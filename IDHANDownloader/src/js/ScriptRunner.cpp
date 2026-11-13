#include "js/ScriptRunner.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <format>
#include <pthread.h>
#include <utility>

#include "session/SessionContext.hpp"

namespace idhan::downloader
{

ScriptRunner::ScriptRunner( Options options, BytecodeCache& bytecode ) : m_options( options ), m_bytecode( bytecode )
{
	m_options.parser_instances = std::max< std::size_t >( 1, m_options.parser_instances );
	spdlog::info(
		"downloader script: up to {} dedicated instances per parser, shared by all sessions",
		m_options.parser_instances );
}

ScriptRunner::~ScriptRunner()
{
	stop();
}

void ScriptRunner::run( Runtime& runtime )
{
	const auto name { std::format( "dl-parser {}", runtime.id ) };
	::pthread_setname_np( ::pthread_self(), name.c_str() );
	runtime.scripts = std::make_unique< ScriptContext >( m_options.script, m_bytecode, runtime.activity, m_key_values );
	while ( true )
	{
		runtime.available.acquire();
		RuntimeEvent event;
		{
			const std::scoped_lock lock { m_mutex };
			event = std::move( runtime.events.front() );
			runtime.events.pop_front();
		}
		const auto* reason { std::get_if< WakeReason >( &event ) };
		if ( reason != nullptr && *reason == WakeReason::STOP ) break;

		const bool ready { processEvent( runtime, std::move( event ) ) };
		if ( ready )
		{
			const std::scoped_lock lock { m_mutex };
			if ( runtime.events.empty() ) enqueueEvent( runtime, WakeReason::READY_MICROTASKS );
		}
	}

	discardAll( runtime );
}

void ScriptRunner::enqueueEvent( Runtime& runtime, RuntimeEvent event )
{
	runtime.events.push_back( std::move( event ) );
	runtime.available.release();
}

bool ScriptRunner::processEvent( Runtime& runtime, RuntimeEvent event )
{
	auto* completion { std::get_if< ScriptCompletion >( &event ) };
	if ( completion != nullptr ) completion->handle->impl().completePendingOperation( completion->pending );

	auto* submitted { std::get_if< ScriptWork >( &event ) };
	if ( submitted != nullptr )
	{
		const std::scoped_lock lock { m_mutex };
		runtime.work.push_back( std::move( *submitted ) );
	}

	bool ready { advance( runtime ) };
	std::optional< ScriptWork > work;
	bool reserved {};
	{
		const std::scoped_lock lock { m_mutex };
		// Rotate past sessions whose HTTP budget is full, so they cannot block others.
		const auto waiting { runtime.work.size() };
		for ( std::size_t index {}; index < waiting; ++index )
		{
			auto candidate { std::move( runtime.work.front() ) };
			runtime.work.pop_front();
			if ( !candidate.handle->impl().acceptsWork() )
			{
				work = std::move( candidate );
				break;
			}

			if ( candidate.handle->impl().reserveStart() )
			{
				reserved = true;
				work = std::move( candidate );
				break;
			}

			runtime.work.push_back( std::move( candidate ) );
		}
	}
	if ( work.has_value() )
	{
		auto& session { work.value().handle->impl() };
		if ( !session.acceptsWork() )
		{
			if ( reserved ) session.releaseStart();

			session.retireQueued( work.value().id );
			const std::scoped_lock lock { m_mutex };
			--runtime.load;
		}
		else
		{
			auto execution { session.beginWork( *runtime.scripts, work.value(), runtime.id ) };
			if ( execution )
			{
				execution->start_reserved = true;
				runtime.resident.push_back(
					Runtime::Resident { .handle = work.value().handle, .execution = std::move( execution ) } );
			}
			else
			{
				session.releaseStart();
				const std::scoped_lock lock { m_mutex };
				--runtime.load;
			}
		}
		ready = true;
	}

	if ( runtime.scripts->valid() )
	{
		std::string error;
		if ( !runtime.scripts->runReadyMicrotasks( error, 64, std::chrono::milliseconds { 2 } ) )
			spdlog::warn( "downloader script: a microtask threw: {}", error );
	}

	ready = advance( runtime ) || ready;
	return ( runtime.scripts->valid() && JS_IsJobPending( runtime.scripts->runtime() ) ) || ready;
}

bool ScriptRunner::advance( Runtime& runtime )
{
	bool progressed {};
	for ( std::size_t index {}; index < runtime.resident.size(); )
	{
		auto& resident { runtime.resident[ index ] };
		auto& session { resident.handle->impl() };
		auto& execution { *resident.execution };
		const auto before { execution.stage };
		const bool finished { session.step( *runtime.scripts, execution ) };
		session.releaseStartIfReady( execution );
		progressed = progressed || execution.stage != before;
		if ( finished )
			releaseAt( runtime, index );
		else
			++index;
	}
	return progressed;
}

void ScriptRunner::releaseAt( Runtime& runtime, const std::size_t index )
{
	auto& resident { runtime.resident[ index ] };
	resident.handle->impl().releaseWork( *runtime.scripts, *resident.execution );
	// Session destruction can call discard(); release its handle outside m_mutex.
	runtime.resident.erase( runtime.resident.begin() + static_cast< std::ptrdiff_t >( index ) );
	const std::scoped_lock lock { m_mutex };
	--runtime.load;
}

void ScriptRunner::discardAll( Runtime& runtime )
{
	for ( const auto& resident : runtime.resident )
	{
		resident.handle->impl().abandon( *resident.execution );
		resident.handle->impl().releaseWork( *runtime.scripts, *resident.execution );
	}
	runtime.resident.clear();
	std::deque< ScriptWork > work;
	std::deque< RuntimeEvent > events;
	{
		const std::scoped_lock lock { m_mutex };
		work.swap( runtime.work );
		events.swap( runtime.events );
	}
	for ( const auto& pending : work ) pending.handle->impl().retireQueued( pending.id );

	for ( const auto& event : events )
	{
		const auto* pending { std::get_if< ScriptWork >( &event ) };
		if ( pending != nullptr ) pending->handle->impl().retireQueued( pending->id );
	}

	runtime.scripts.reset();
}

void ScriptRunner::stop()
{
	if ( !m_running.exchange( false ) ) return;

	// Synchronize with submit() before taking the stable set of threads to join.
	std::vector< Runtime* > runtimes;
	{
		const std::scoped_lock lock { m_mutex };
		for ( const auto& [ id, runtime ] : m_runtimes )
		{
			runtimes.push_back( runtime.get() );
			// Shutdown has priority over queued work; keep one permit per event.
			runtime->events.emplace_front( WakeReason::STOP );
			runtime->available.release();
		}
	}
	for ( auto* runtime : runtimes )
	{
		if ( runtime->thread.joinable() ) runtime->thread.join();
	}
	m_runtimes.clear();
}

void ScriptRunner::notifySessionReady( const ScriptSession* session )
{
	const std::scoped_lock lock { m_mutex };
	if ( !m_running.load() ) return;

	for ( const auto& [ id, runtime ] : m_runtimes )
	{
		const auto found { runtime->sessions.find( session ) };
		if ( found == runtime->sessions.end() ) continue;

		if ( found->second.expired() )
		{
			runtime->sessions.erase( found );
			continue;
		}

		enqueueEvent( *runtime, WakeReason::SESSION_CAPACITY_CHANGED );
	}
}

bool ScriptRunner::submit( ScriptWork work )
{
	const auto parser { work.route.script.string() };
	{
		const std::scoped_lock lock { m_mutex };
		if ( !m_running.load() || !work.handle->impl().acceptsWork() ) return false;

		Runtime* chosen {};
		std::size_t instances {};
		for ( const auto& [ id, runtime ] : m_runtimes )
		{
			if ( runtime->parser != parser ) continue;

			++instances;
			if ( chosen == nullptr || runtime->load < chosen->load ) chosen = runtime.get();
		}
		if ( chosen == nullptr || ( chosen->load > 0 && instances < m_options.parser_instances ) )
		{
			auto runtime { std::make_unique< Runtime >() };
			runtime->id = m_next_runtime++;
			runtime->parser = parser;
			chosen = runtime.get();
			m_runtimes.emplace( chosen->id, std::move( runtime ) );
			chosen->thread = std::jthread( [ this, chosen ] { run( *chosen ); } );
		}
		work.handle->impl().attachRuntime( chosen->id, chosen->activity );
		std::erase_if( chosen->sessions, []( const auto& entry ) { return entry.second.expired(); } );
		chosen->sessions.insert_or_assign( &work.handle->impl(), work.handle );
		++chosen->load;
		enqueueEvent( *chosen, std::move( work ) );
	}
	return true;
}

bool ScriptRunner::complete( const std::size_t runtime, ScriptCompletion completion )
{
	{
		const std::scoped_lock lock { m_mutex };
		if ( !m_running.load() ) return false;

		const auto found { m_runtimes.find( runtime ) };
		if ( found == m_runtimes.end() ) return false;

		enqueueEvent( *found->second, std::move( completion ) );
	}
	return true;
}

void ScriptRunner::discard( const SessionContext* session )
{
	std::deque< ScriptWork > dropped;
	{
		const std::scoped_lock lock { m_mutex };
		if ( !m_running.load() ) return;

		for ( const auto& [ id, runtime ] : m_runtimes )
		{
			if ( !runtime->sessions.contains( &session->impl() ) ) continue;

			for ( auto event { runtime->events.begin() }; event != runtime->events.end(); )
			{
				auto* work { std::get_if< ScriptWork >( &*event ) };
				if ( work == nullptr || work->handle.get() != session )
				{
					++event;
					continue;
				}

				dropped.push_back( std::move( *work ) );
				// The consumer may already hold this event's permit. Replace it rather
				// than removing a queue slot and desynchronizing the semaphore count.
				*event = WakeReason::SESSION_CAPACITY_CHANGED;
				++event;
				--runtime->load;
			}
			for ( auto work { runtime->work.begin() }; work != runtime->work.end(); )
			{
				if ( work->handle.get() != session )
				{
					++work;
					continue;
				}

				dropped.push_back( std::move( *work ) );
				work = runtime->work.erase( work );
				--runtime->load;
			}
			enqueueEvent( *runtime, WakeReason::SESSION_CAPACITY_CHANGED );
		}
	}
	for ( const auto& work : dropped ) work.handle->impl().retireQueued( work.id );
}

} // namespace idhan::downloader
