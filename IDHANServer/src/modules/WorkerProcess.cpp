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
#include <ranges>
#include <signal.h>
#include <span>
#include <unistd.h>

#include "logging/log.hpp"

namespace idhan::modules
{

//! The descriptor the runner is told to use.
constexpr int CHILD_CHANNEL_FD { 3 };

//! Where a call's continuation should be resumed, given the thread that issued the call.
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

//! \warning Must not run on the worker's own IO thread. The join below would be a self-join, and
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

	const int child_end { channel->second.get() };

	const pid_t parent_pid { ::getpid() };

	const pid_t pid { ::fork() };
	if ( pid < 0 ) return std::unexpected( std::format( "fork failed: {}", std::strerror( errno ) ) );

	if ( pid == 0 )
	{
		if ( child_end == CHILD_CHANNEL_FD )
		{
			if ( ::fcntl( CHILD_CHANNEL_FD, F_SETFD, 0 ) < 0 ) ::_exit( 126 );
		}
		else if ( ::dup2( child_end, CHILD_CHANNEL_FD ) < 0 )
		{
			::_exit( 126 );
		}

		if ( ::close_range( CHILD_CHANNEL_FD + 1, ~0U, 0 ) < 0 ) ::_exit( 126 );

		if ( ::prctl( PR_SET_DUMPABLE, 1, 0, 0, 0 ) < 0 ) ::_exit( 126 );

		if ( ::prctl( PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0 ) < 0 ) ::_exit( 126 );

		if ( ::prctl( PR_SET_PDEATHSIG, SIGKILL, 0, 0, 0 ) < 0 ) ::_exit( 126 );

		if ( ::getppid() != parent_pid ) ::_exit( 0 );

		if ( harden_workers )
		{
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

	std::vector< int > fds {};

	if ( input != nullptr )
	{
		body[ ipc::field::FILE_SIZE ] = Json::UInt64 { input->size() };

		fds.emplace_back( input->fd() );

		const std::lock_guard< std::mutex > guard { m_calls_mutex };
		m_call_inputs.emplace(
			call_id, InFlightInput { .input = std::move( input ), .mime = body[ ipc::field::MIME ].asString() } );
	}

	//! Registers the call, posts it, and parks the coroutine until the result comes back.
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

				auto* const loop { resumptionLoop() };

				worker->m_calls.emplace(
					call_id, PendingCall { .continuation = handle, .loop = loop, .outcome = outcome } );

				worker->m_last_activity = std::chrono::steady_clock::now();
			}

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
