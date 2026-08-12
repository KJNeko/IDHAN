#include "WorkerProcess.hpp"

#include <drogon/HttpAppFramework.h>
#include <sys/eventfd.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <trantor/net/EventLoop.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <format>
#include <poll.h>
// std::views::values in failAll(). Included explicitly rather than leaned on through <algorithm>:
// libstdc++ 15 pulls <ranges> in that way and GCC 14 does not, so the transitive path builds here
// and fails in the Ubuntu 24.04 container.
#include <ranges>
#include <signal.h>
#include <span>
#include <unistd.h>

#include "logging/log.hpp"

namespace idhan::modules
{

//! The descriptor the runner is told to use. Chosen rather than inherited-as-is because dup2 onto a
//! fixed number is the only part of the child setup that has to happen after fork and before exec.
constexpr int CHILD_CHANNEL_FD { 3 };

//! Where a call's continuation should be resumed, given the thread that issued the call.
/** Falls back to the main loop when the caller is not on one, rather than leaving it null: a null
 *  loop used to mean "resume inline", and inline means on the IO thread, which is the one place a
 *  continuation must never run. The resumed coroutine owns the shared_ptr to this worker, so the
 *  frame's destruction -- and with it ~WorkerProcess, which joins the IO thread -- would land on the
 *  IO thread itself. Joining yourself is EDEADLK, which is a throw out of a noexcept destructor and
 *  takes the whole server with it. */
[[nodiscard]] trantor::EventLoop* resumptionLoop()
{
	auto* const current { trantor::EventLoop::getEventLoopOfCurrentThread() };
	return current != nullptr ? current : drogon::app().getLoop();
}

//! Resumes a parked call, always on an event loop and never on the calling thread's stack.
void resumeOnLoop( std::coroutine_handle<> continuation, trantor::EventLoop* loop )
{
	if ( !continuation ) return;
	if ( loop == nullptr ) loop = drogon::app().getLoop();

	loop->queueInLoop( [ continuation ]() mutable { continuation.resume(); } );
}


WorkerProcess::WorkerProcess( WorkerSettings settings, CallbackHandler on_callback ) :
  m_settings( std::move( settings ) ),
  m_on_callback( std::move( on_callback ) )
{}

//! \warning Must not run on the worker's own IO thread -- the join below would be a self-join, and
//!          EDEADLK out of a destructor aborts the process. Nothing that could hold the last
//!          reference is ever resumed on the IO thread; see resumptionLoop().
WorkerProcess::~WorkerProcess()
{
	terminate( "worker owner destroyed", Termination::EXPECTED );

	if ( m_io.joinable() )
	{
		m_io.request_stop();
		if ( m_wakeup )
		{
			const std::uint64_t one { 1 };
			[[maybe_unused]] const auto ignored { ::write( m_wakeup.get(), &one, sizeof( one ) ) };
		}
		m_io.join();
	}
}

std::expected< void, std::string > WorkerProcess::start()
{
	auto channel { ipc::createChannel() };
	if ( !channel ) return std::unexpected( channel.error() );

	ipc::UniqueFd wakeup { ::eventfd( 0, EFD_CLOEXEC | EFD_NONBLOCK ) };
	if ( !wakeup ) return std::unexpected( std::format( "eventfd failed: {}", std::strerror( errno ) ) );

	const auto pool_threads { std::to_string( m_settings.pool_threads ) };
	const auto heartbeat { std::to_string( m_settings.heartbeat_interval.count() ) };
	const auto library { m_settings.library.string() };
	const auto runner { m_settings.runner.string() };
	const auto child_fd { std::to_string( CHILD_CHANNEL_FD ) };
	const auto log_level { m_settings.log_level };
	const bool describe_only { m_settings.describe_only };
#ifdef IDHAN_HARDEN
	constexpr bool harden_workers { true };
#else
	constexpr bool harden_workers { false };
#endif

	// Read before forking: touching m_settings in the child would be reading through `this` after a
	// fork from a threaded parent, and every value the child needs is cheap to copy out here.
	const int child_end { channel->second.get() };

	// Our own pid, so the child can tell whether it is still ours. Compared by value rather than
	// against 1, because in a container the server *is* pid 1 and every child would look orphaned.
	const pid_t parent_pid { ::getpid() };

	const pid_t pid { ::fork() };
	if ( pid < 0 ) return std::unexpected( std::format( "fork failed: {}", std::strerror( errno ) ) );

	if ( pid == 0 )
	{
		// Child. Only async-signal-safe work here: the parent has threads, so anything that could
		// take a lock held by one of them at fork time would deadlock.
		if ( child_end == CHILD_CHANNEL_FD )
		{
			// socketpair gave us exactly the number we wanted. dup2 onto the same descriptor is a
			// no-op that does NOT clear FD_CLOEXEC, so the socket would vanish at exec and the runner
			// would come up with nothing to talk to. Clear the flag by hand instead.
			if ( ::fcntl( CHILD_CHANNEL_FD, F_SETFD, 0 ) < 0 ) ::_exit( 126 );
		}
		else if ( ::dup2( child_end, CHILD_CHANNEL_FD ) < 0 )
		{
			::_exit( 126 );
		}

		// Everything above the channel goes. The server has a database pool, cluster files and the
		// other workers' sockets open, and none of them are reliably FD_CLOEXEC -- libpq's are not.
		// Without this a compromised worker inherits a live connection to the database.
		if ( ::close_range( CHILD_CHANNEL_FD + 1, ~0U, 0 ) < 0 ) ::_exit( 126 );

		// Undo the server's PR_SET_DUMPABLE(0) (see ModuleLoader::applyHardening) for this child only.
		// That flag is inherited across fork and still set at execve, and a non-dumpable image execs
		// in the kernel's secure-exec mode -- where the dynamic linker stops honouring the
		// $ORIGIN-relative RUNPATH that finds libIDHAN.so next to the runner. The worker then dies
		// before main(), so it cannot report anything, and all the host sees is a closed channel.
		//
		// Costs nothing that was being bought: the point of the server being non-dumpable is that
		// nothing can ptrace *the server*, and ptrace_may_access tests the target's flag, not the
		// caller's. The server stays non-dumpable; worker cores are handled by RLIMIT_CORE below.
		if ( ::prctl( PR_SET_DUMPABLE, 1, 0, 0, 0 ) < 0 ) ::_exit( 126 );

		// No setuid or file-capability binary this process execs can gain privilege. Cheap on its
		// own, and a prerequisite for the follow-up spec's seccomp filter, which cannot be installed
		// unprivileged without it.
		if ( ::prctl( PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0 ) < 0 ) ::_exit( 126 );

		// Workers currently outlive a SIGKILLed server, which leaves orphaned processes holding
		// mappings of cluster files.
		if ( ::prctl( PR_SET_PDEATHSIG, SIGKILL, 0, 0, 0 ) < 0 ) ::_exit( 126 );

		// PDEATHSIG is delivered on the death of the forking *thread*, and it is armed after the
		// fork -- so a parent that died in between would never signal us at all. Re-checking closes
		// that window: a parent id that is no longer the one that forked us means we were reparented.
		//
		// Compared against the recorded pid, never against 1. Under Docker the server is pid 1, so
		// `getppid() == 1` is true for every healthy worker -- that test exited each one before it
		// could exec, and every module library was skipped as having died before announcing itself.
		if ( ::getppid() != parent_pid ) ::_exit( 0 );

		if ( harden_workers )
		{
			// A core from a worker contains the decoded media it was working on, written wherever
			// the pattern points. Off by default outside Debug; see IDHAN_HARDEN.
			const rlimit no_core { .rlim_cur = 0, .rlim_max = 0 };
			if ( ::setrlimit( RLIMIT_CORE, &no_core ) < 0 ) ::_exit( 126 );
		}

		if ( describe_only )
			::execl(
				runner.c_str(),
				runner.c_str(),
				"--library",
				library.c_str(),
				"--socket-fd",
				child_fd.c_str(),
				"--log-level",
				log_level.c_str(),
				"--describe",
				static_cast< char* >( nullptr ) );
		else
			::execl(
				runner.c_str(),
				runner.c_str(),
				"--library",
				library.c_str(),
				"--socket-fd",
				child_fd.c_str(),
				"--pool-threads",
				pool_threads.c_str(),
				"--heartbeat-ms",
				heartbeat.c_str(),
				"--log-level",
				log_level.c_str(),
				static_cast< char* >( nullptr ) );

		::_exit( 127 );
	}

	m_pid = pid;
	m_socket = std::move( channel->first );
	m_wakeup = std::move( wakeup );

	if ( const auto configured { ipc::setNonBlocking( m_socket.get() ) }; !configured )
		return std::unexpected( configured.error() );

	m_alive.store( true );
	m_last_heartbeat = std::chrono::steady_clock::now();
	m_last_activity = m_last_heartbeat;

	m_io = std::jthread( [ this ]( const std::stop_token& stop ) { ioLoop( stop ); } );

	// An interrogator is an implementation detail of startup -- one per library, immediately followed
	// by the Registered module lines that actually tell you what was found. Only a serving worker is
	// worth an info line.
	if ( m_settings.describe_only )
		log::debug( "Interrogating module library {}", m_settings.library.string() );
	else
		log::info( "Started module {}", m_settings.library.string() );

	return {};
}

void WorkerProcess::ioLoop( const std::stop_token& stop )
{
	while ( !stop.stop_requested() && m_alive.load() )
	{
		bool want_write { false };
		{
			const std::lock_guard< std::mutex > guard { m_write_mutex };
			const auto drained { m_writer.drain( m_socket.get() ) };
			if ( !drained )
			{
				terminate( drained.error() );
				return;
			}
			want_write = !*drained;
		}

		auto frames { m_reader.read( m_socket.get() ) };
		if ( !frames )
		{
			terminate( frames.error() );
			return;
		}

		for ( auto& frame : *frames ) handleFrame( std::move( frame ) );

		if ( m_reader.atEof() )
		{
			// An interrogator is *supposed* to end this way: --describe announces the manifest and
			// exits, so its channel closing is the successful path, not a death. Anything else closing
			// its channel has genuinely gone away mid-service.
			const bool announced_and_done { m_settings.describe_only && manifestSeen() };

			terminate(
				announced_and_done ? "interrogator finished announcing" : "worker closed the channel",
				announced_and_done ? Termination::EXPECTED : Termination::FAILURE );
			return;
		}

		checkLiveness();

		std::array< pollfd, 2 > waiting {
			{ { .fd = m_socket.get(),
			    .events = static_cast< short >( POLLIN | ( want_write ? POLLOUT : 0 ) ),
			    .revents = 0 },
			  { .fd = m_wakeup.get(), .events = POLLIN, .revents = 0 } }
		};

		// Capped so the liveness check still happens on an otherwise silent channel.
		const int ready { ::poll( waiting.data(), waiting.size(), 200 ) };

		if ( ready < 0 && errno != EINTR )
		{
			terminate( std::format( "poll on the worker channel failed: {}", std::strerror( errno ) ) );
			return;
		}

		if ( ( waiting[ 1 ].revents & POLLIN ) != 0 )
		{
			std::uint64_t drained { 0 };
			[[maybe_unused]] const auto ignored { ::read( m_wakeup.get(), &drained, sizeof( drained ) ) };
		}
	}
}

void WorkerProcess::handleFrame( ipc::Frame&& frame )
{
	const auto type { ipc::fromWire< ipc::MessageType >( frame.body[ ipc::field::TYPE ] ) };
	if ( !type ) return;

	switch ( *type )
	{
		case ipc::MessageType::MANIFEST:
			{
				std::vector< ipc::ManifestEntry > entries {};
				for ( const auto& entry : frame.body[ ipc::field::MODULES ] )
				{
					auto parsed { ipc::manifestEntryFromJson( entry ) };
					if ( !parsed )
					{
						log::warn(
							"Worker for {} sent an unparseable manifest: {}",
							m_settings.library.string(),
							parsed.error() );
						continue;
					}
					entries.emplace_back( std::move( *parsed ) );
				}

				std::string mismatch {};
				{
					const std::lock_guard< std::mutex > guard { m_manifest_mutex };
					m_manifest = std::move( entries );
					m_signature = ipc::manifestSignature( m_manifest );
					m_manifest_seen = true;

					if ( !m_settings.expected_signature.empty() && m_signature != m_settings.expected_signature )
						mismatch = std::format(
							"{} no longer matches the manifest it was registered with; module indexes are stale",
							m_settings.library.string() );
				}

				// Checked here rather than by blocking a caller until the manifest lands: dispatch sends
				// its call the moment the worker is spawned, and a stale library fails those calls loudly
				// instead of quietly running the wrong module.
				if ( !mismatch.empty() ) terminate( mismatch );

				return;
			}
		case ipc::MessageType::HEARTBEAT:
			{
				m_rss_kb.store( frame.body[ ipc::field::RSS_KB ].asUInt64() );
				m_active_calls.store( frame.body[ ipc::field::ACTIVE_CALLS ].asUInt64() );
				m_last_heartbeat = std::chrono::steady_clock::now();
				return;
			}
		case ipc::MessageType::RESULT:
			{
				const std::uint64_t call_id { frame.body[ ipc::field::CALL_ID ].asUInt64() };

				CallOutcome outcome {};
				outcome.ok = frame.body[ ipc::field::OK ].asBool();
				outcome.error = frame.body[ ipc::field::ERROR ].asString();
				outcome.body = std::move( frame.body );

				if ( !frame.fds.empty() )
				{
					auto adopted { ipc::Blob::adopt( std::move( frame.fds.front() ) ) };
					if ( adopted )
						outcome.blob = std::move( *adopted );
					else if ( outcome.ok )
					{
						outcome.ok = false;
						outcome.error = adopted.error();
					}
				}

				finish( call_id, std::move( outcome ) );
				return;
			}
		case ipc::MessageType::CALLBACK:
			{
				// Handled off this thread: servicing it needs the database and the module registry, and
				// the IO thread must stay free to keep reading -- the worker is blocked on this answer.
				if ( m_on_callback ) m_on_callback( shared_from_this(), std::move( frame ) );
				return;
			}
		case ipc::MessageType::CALL:
			[[fallthrough]];
		case ipc::MessageType::CALLBACK_RESULT:
			[[fallthrough]];
		case ipc::MessageType::RECLAIM:
			[[fallthrough]];
		case ipc::MessageType::SHUTDOWN:
			log::warn( "Worker for {} sent a worker-bound message", m_settings.library.string() );
			return;
	}
}

void WorkerProcess::finish( const std::uint64_t call_id, CallOutcome outcome )
{
	PendingCall pending {};
	{
		const std::lock_guard< std::mutex > guard { m_calls_mutex };
		const auto found { m_calls.find( call_id ) };
		if ( found == m_calls.end() ) return;

		pending = std::move( found->second );
		m_calls.erase( found );
		m_last_activity = std::chrono::steady_clock::now();
	}

	*pending.outcome = std::move( outcome );

	resumeOnLoop( pending.continuation, pending.loop );
}

void WorkerProcess::failAll( const std::string& reason, const bool died )
{
	std::vector< PendingCall > pending {};
	{
		const std::lock_guard< std::mutex > guard { m_calls_mutex };
		for ( auto& call : m_calls | std::views::values ) pending.emplace_back( std::move( call ) );
		m_calls.clear();
	}

	for ( auto& call : pending )
	{
		call.outcome->ok = false;
		call.outcome->error = reason;
		call.outcome->worker_died = died;

		resumeOnLoop( call.continuation, call.loop );
	}
}

void WorkerProcess::checkLiveness()
{
	const auto now { std::chrono::steady_clock::now() };

	// The only guard left, and it deliberately asks one question: does this process still exist? A
	// worker answers heartbeats from its IO loop, which never runs module code, so a heartbeat keeps
	// arriving however long the backlog is. Slowness is not a failure here -- only death is.
	if ( now - m_last_heartbeat > m_settings.liveness_grace )
		terminate(
			std::format(
				"worker for {} missed heartbeats for {}ms",
				m_settings.library.string(),
				std::chrono::duration_cast< std::chrono::milliseconds >( now - m_last_heartbeat ).count() ) );
}

void WorkerProcess::terminate( const std::string& reason, const Termination kind )
{
	if ( !m_alive.exchange( false ) ) return;

	if ( kind == Termination::FAILURE )
		log::warn( "Terminating module worker for {}: {}", m_settings.library.string(), reason );
	else
		log::debug( "Retiring module worker for {}: {}", m_settings.library.string(), reason );

	if ( m_pid > 0 )
	{
		::kill( m_pid, SIGKILL );

		int status { 0 };
		::waitpid( m_pid, &status, 0 );

		// Reported, because every failure between fork and exec is a silent _exit and the worker's own
		// logging cannot cover them -- it does not exist yet. This status is the only evidence of what
		// happened, and discarding it left "worker closed the channel" as the whole diagnosis.
		//
		//   126     the child setup failed: close_range, PR_SET_NO_NEW_PRIVS, PR_SET_PDEATHSIG, or
		//           the RLIMIT_CORE clamp
		//   127     execl failed -- the runner path is wrong or not executable
		//   0       the child decided it had been reparented, or exited cleanly
		//   SIGKILL either this terminate(), or PR_SET_PDEATHSIG firing because the *thread* that
		//           forked has since exited
		if ( WIFEXITED( status ) && WEXITSTATUS( status ) != 0 )
			log::warn(
				"Module worker for {} exited with status {}", m_settings.library.string(), WEXITSTATUS( status ) );
		else if ( WIFSIGNALED( status ) && WTERMSIG( status ) != SIGKILL )
			log::warn( "Module worker for {} died on signal {}", m_settings.library.string(), WTERMSIG( status ) );

		m_pid = -1;
	}

	failAll( reason, true );
}

std::chrono::steady_clock::time_point WorkerProcess::lastActivity()
{
	const std::lock_guard< std::mutex > guard { m_calls_mutex };
	return m_last_activity;
}

std::vector< ipc::ManifestEntry > WorkerProcess::manifest()
{
	const std::lock_guard< std::mutex > guard { m_manifest_mutex };
	return m_manifest;
}

std::string WorkerProcess::signature()
{
	const std::lock_guard< std::mutex > guard { m_manifest_mutex };
	return m_signature;
}

bool WorkerProcess::manifestSeen()
{
	const std::lock_guard< std::mutex > guard { m_manifest_mutex };
	return m_manifest_seen;
}

std::expected< std::vector< ipc::ManifestEntry >, std::string > WorkerProcess::awaitManifest(
	const std::chrono::milliseconds timeout )
{
	const auto deadline { std::chrono::steady_clock::now() + timeout };

	while ( std::chrono::steady_clock::now() < deadline )
	{
		{
			const std::lock_guard< std::mutex > guard { m_manifest_mutex };
			if ( m_manifest_seen ) return m_manifest;
		}

		if ( !m_alive.load() )
			return std::unexpected(
				std::format( "worker for {} died before announcing itself", m_settings.library.string() ) );

		std::this_thread::sleep_for( std::chrono::milliseconds { 5 } );
	}

	return std::unexpected(
		std::format(
			"worker for {} did not announce itself within {}ms", m_settings.library.string(), timeout.count() ) );
}

std::expected< void, std::string > WorkerProcess::post( const Json::Value& body, const std::span< const int > fds )
{
	if ( !m_alive.load() ) return std::unexpected( std::string { "worker is not running" } );

	{
		const std::lock_guard< std::mutex > guard { m_write_mutex };
		const auto queued { m_writer.enqueue( body, fds ) };
		if ( !queued ) return std::unexpected( queued.error() );
	}

	const std::uint64_t one { 1 };
	if ( ::write( m_wakeup.get(), &one, sizeof( one ) ) < 0 && errno != EAGAIN )
		return std::unexpected( std::format( "waking the worker IO thread failed: {}", std::strerror( errno ) ) );

	return {};
}

InFlightInput WorkerProcess::inputForCall( const std::uint64_t call_id )
{
	const std::lock_guard< std::mutex > guard { m_calls_mutex };

	const auto found { m_call_inputs.find( call_id ) };

	return found == m_call_inputs.end() ? InFlightInput {} : found->second;
}

IDHANTask< std::shared_ptr< CallOutcome > > WorkerProcess::call(
	Json::Value body,
	std::shared_ptr< const CallInput > input )
{
	// Assigned here rather than by the caller: this is the map the id has to be unique in.
	const std::uint64_t call_id { m_next_call_id.fetch_add( 1 ) };
	body[ ipc::field::CALL_ID ] = Json::UInt64 { call_id };

	auto outcome { std::make_shared< CallOutcome >() };

	// A null input used to be rejected outright, because every op operated on a file. EMBED_TEXT
	// does not: it carries a phrase and nothing else, so there is no descriptor to send, nothing to
	// register for INPUT_REF reuse, and no size to declare.
	std::vector< int > fds {};

	if ( input != nullptr )
	{
		body[ ipc::field::FILE_SIZE ] = Json::UInt64 { input->size() };

		// Borrowed for the length of the send; the input owns it and outlives the call, which is also
		// what lets a nested call reuse the same descriptor through INPUT_REF.
		fds.emplace_back( input->fd() );

		// Registered for the whole call so a module handing its own input back through a callback can
		// be answered with the descriptor we already hold, rather than the file being shipped a
		// second time.
		const std::lock_guard< std::mutex > guard { m_calls_mutex };
		m_call_inputs.emplace(
			call_id, InFlightInput { .input = std::move( input ), .mime = body[ ipc::field::MIME ].asString() } );
	}

	//! Registers the call, posts it, and parks the coroutine until the result comes back.
	/** A local class so it can reach the worker's internals; it has the same access as the member
	 *  function enclosing it. */
	struct Suspender
	{
		WorkerProcess* worker;
		std::uint64_t call_id;
		std::shared_ptr< CallOutcome > outcome;
		Json::Value* body;
		const std::vector< int >* fds;

		[[nodiscard]] bool await_ready() const noexcept { return false; }

		bool await_suspend( std::coroutine_handle<> handle ) const
		{
			{
				const std::lock_guard< std::mutex > guard { worker->m_calls_mutex };

				if ( !worker->m_alive.load() )
				{
					outcome->ok = false;
					outcome->error = "worker is not running";
					outcome->worker_died = true;
					// Not suspending: the coroutine carries on with the failure already recorded.
					return false;
				}

				// Never null: a call issued off a loop thread still has to be resumed on one, because
				// the only other thread available is the callee's IO thread.
				auto* const loop { resumptionLoop() };

				worker->m_calls.emplace(
					call_id, PendingCall { .continuation = handle, .loop = loop, .outcome = outcome } );

				worker->m_last_activity = std::chrono::steady_clock::now();
			}

			// Posted only after the call is registered: the worker can answer on the IO thread
			// before post() has even returned, and the result needs a pending entry to land in.
			if ( const auto posted { worker->post( *body, *fds ) }; !posted )
			{
				CallOutcome failure {};
				failure.ok = false;
				failure.error = posted.error();
				failure.worker_died = true;
				worker->finish( call_id, std::move( failure ) );
			}

			return true;
		}

		void await_resume() const noexcept {}
	};

	co_await Suspender { .worker = this, .call_id = call_id, .outcome = outcome, .body = &body, .fds = &fds };

	{
		const std::lock_guard< std::mutex > guard { m_calls_mutex };
		m_call_inputs.erase( call_id );
	}

	co_return outcome;
}

} // namespace idhan::modules
