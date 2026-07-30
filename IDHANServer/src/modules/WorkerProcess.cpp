//
// Created by kj16609 on 7/28/26.
//

#include "WorkerProcess.hpp"

#include <drogon/HttpAppFramework.h>
#include <sys/eventfd.h>
#include <sys/wait.h>
#include <trantor/net/EventLoop.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <format>
#include <poll.h>
#include <signal.h>
#include <span>
#include <unistd.h>

#include "logging/log.hpp"

namespace idhan::modules
{

namespace
{

//! The descriptor the runner is told to use. Chosen rather than inherited-as-is because dup2 onto a
//! fixed number is the only part of the child setup that has to happen after fork and before exec.
constexpr int CHILD_CHANNEL_FD { 3 };

} // namespace

WorkerProcess::WorkerProcess( WorkerSettings settings, CallbackHandler on_callback ) :
  m_settings( std::move( settings ) ),
  m_on_callback( std::move( on_callback ) )
{}

WorkerProcess::~WorkerProcess()
{
	terminate( "worker owner destroyed" );

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
	const bool describe_only { m_settings.describe_only };

	// Read before forking: touching m_settings in the child would be reading through `this` after a
	// fork from a threaded parent, and every value the child needs is cheap to copy out here.
	const int child_end { channel->second.get() };

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

		if ( describe_only )
			::execl(
				runner.c_str(),
				runner.c_str(),
				"--library",
				library.c_str(),
				"--socket-fd",
				child_fd.c_str(),
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
			terminate( "worker closed the channel" );
			return;
		}

		checkDeadlines();

		std::array< pollfd, 2 > waiting {
			{ { .fd = m_socket.get(),
			    .events = static_cast< short >( POLLIN | ( want_write ? POLLOUT : 0 ) ),
			    .revents = 0 },
			  { .fd = m_wakeup.get(), .events = POLLIN, .revents = 0 } }
		};

		// Capped so deadline and liveness checks still happen on an otherwise silent channel.
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
	const auto type { ipc::messageTypeFromString( frame.body[ ipc::field::TYPE ].asString() ) };
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
		case ipc::MessageType::ACK:
			{
				const std::uint64_t call_id { frame.body[ ipc::field::CALL_ID ].asUInt64() };
				const auto estimate { std::chrono::milliseconds { frame.body[ ipc::field::ESTIMATE_MS ].asUInt64() } };

				// The module's own estimate, scaled and capped. Scaled because an estimate is a guess and
				// killing a module that is merely slower than it predicted is worse than waiting; capped
				// because a module that returns something absurd must not disable the watchdog.
				const auto scaled {
					std::chrono::duration_cast< std::chrono::milliseconds >( estimate * m_settings.timeout_multiplier )
				};
				const auto budget { std::min( scaled, m_settings.max_timeout ) };

				const std::lock_guard< std::mutex > guard { m_calls_mutex };
				if ( const auto found { m_calls.find( call_id ) }; found != m_calls.end() )
				{
					found->second.acked = true;
					found->second.deadline = std::chrono::steady_clock::now() + budget;
				}
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

	if ( pending.continuation )
	{
		auto continuation { pending.continuation };
		if ( pending.loop != nullptr )
			pending.loop->queueInLoop( [ continuation ]() mutable { continuation.resume(); } );
		else
			continuation.resume();
	}
}

void WorkerProcess::failAll( const std::string& reason, const bool died )
{
	std::vector< PendingCall > pending {};
	{
		const std::lock_guard< std::mutex > guard { m_calls_mutex };
		for ( auto& [ id, call ] : m_calls ) pending.emplace_back( std::move( call ) );
		m_calls.clear();
	}

	for ( auto& call : pending )
	{
		call.outcome->ok = false;
		call.outcome->error = reason;
		call.outcome->worker_died = died;

		if ( call.continuation )
		{
			auto continuation { call.continuation };
			if ( call.loop != nullptr )
				call.loop->queueInLoop( [ continuation ]() mutable { continuation.resume(); } );
			else
				continuation.resume();
		}
	}
}

void WorkerProcess::checkDeadlines()
{
	const auto now { std::chrono::steady_clock::now() };

	// Two independent guards. The deadline covers a module that is wedged inside a call; the
	// heartbeat covers a process that has stopped existing in any useful sense -- and a heartbeat
	// only proves the IO thread is alive, which is why the deadline is still needed.
	if ( now - m_last_heartbeat > m_settings.liveness_grace )
	{
		terminate(
			std::format(
				"worker for {} missed heartbeats for {}ms",
				m_settings.library.string(),
				std::chrono::duration_cast< std::chrono::milliseconds >( now - m_last_heartbeat ).count() ) );
		return;
	}

	bool expired { false };
	{
		const std::lock_guard< std::mutex > guard { m_calls_mutex };
		for ( const auto& [ id, call ] : m_calls )
		{
			if ( call.acked && now > call.deadline )
			{
				expired = true;
				break;
			}
		}
	}

	if ( expired )
		terminate( std::format( "worker for {} exceeded its own duration estimate", m_settings.library.string() ) );
}

void WorkerProcess::terminate( const std::string& reason )
{
	if ( !m_alive.exchange( false ) ) return;

	log::warn( "Terminating module worker for {}: {}", m_settings.library.string(), reason );

	if ( m_pid > 0 )
	{
		::kill( m_pid, SIGKILL );

		int status { 0 };
		::waitpid( m_pid, &status, 0 );
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

IDHANTask< std::shared_ptr< CallOutcome > > WorkerProcess::call( Json::Value body, std::vector< int > fds )
{
	// Assigned here rather than by the caller: this is the map the id has to be unique in.
	const std::uint64_t call_id { m_next_call_id.fetch_add( 1 ) };
	body[ ipc::field::CALL_ID ] = Json::UInt64 { call_id };

	auto outcome { std::make_shared< CallOutcome >() };

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

				// Null when this coroutine was resumed off a loop thread; finish() then resumes
				// inline rather than pretending it knows where to go.
				auto* const loop { trantor::EventLoop::getEventLoopOfCurrentThread() };

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

	co_return outcome;
}

} // namespace idhan::modules
