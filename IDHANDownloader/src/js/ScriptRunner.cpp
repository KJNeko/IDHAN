#include "js/ScriptRunner.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <pthread.h>
#include <utility>

#include "SessionContext.hpp"

namespace idhan::downloader
{

ScriptRunner::Worker::Worker( ScriptRunner& runner, const std::size_t index ) : m_runner( runner ), m_index( index )
{
	m_thread = std::jthread( [ this ] { run(); } );
}

ScriptRunner::Worker::~Worker()
{
	join();
}

void ScriptRunner::Worker::join()
{
	notify();

	if ( m_thread.joinable() ) m_thread.join();
}

void ScriptRunner::Worker::notify()
{
	{
		const std::scoped_lock lock { m_mutex };
		m_woken = true;
	}

	m_wakeup.notify_all();
}

void ScriptRunner::Worker::deliver( ScriptCompletion completion )
{
	{
		const std::scoped_lock lock { m_mutex };
		m_inbox.emplace_back( std::move( completion ) );
		m_woken = true;
	}

	m_wakeup.notify_all();
}

void ScriptRunner::Worker::run()
{
	::pthread_setname_np( ::pthread_self(), "downloader-script" );
	m_scripts = std::make_unique< ScriptContext >( m_runner.m_options.script, m_runner.m_bytecode );

	while ( m_runner.m_running.load() )
	{
		std::string error {};

		if ( !m_scripts->pumpJobs( error ) ) spdlog::warn( "downloader script: a microtask threw: {}", error );

		drainInbox();

		if ( !m_scripts->pumpJobs( error ) ) spdlog::warn( "downloader script: a microtask threw: {}", error );

		advance();
		startWork();
		advance();

		std::unique_lock lock { m_mutex };

		if ( m_woken || !m_inbox.empty() )
		{
			m_woken = false;
			continue;
		}

		m_wakeup.wait_for( lock, std::chrono::milliseconds { 250 }, [ this ] { return m_woken || !m_inbox.empty(); } );
		m_woken = false;
	}

	discardAll();
	m_scripts.reset();
}

void ScriptRunner::Worker::drainInbox()
{
	std::deque< ScriptCompletion > taken {};

	{
		const std::scoped_lock lock { m_mutex };
		taken.swap( m_inbox );
	}

	for ( ScriptCompletion& completion : taken ) completion.handle->impl().settle( completion.pending );
}

void ScriptRunner::Worker::startWork()
{
	while ( m_runner.m_running.load() )
	{
		auto work { m_runner.take() };

		if ( !work.has_value() ) return;

		SessionContext::Impl& session { work->handle->impl() };
		auto execution { session.beginWork( *m_scripts, *work, m_index ) };

		if ( !execution )
		{
			session.releaseStart();
			continue;
		}

		m_resident.emplace_back( std::move( execution ) );
		m_handles.emplace_back( work->handle );

		// Count parser requests before releasing the reserved start slot.
		const bool finished { session.step( *m_scripts, *m_resident.back() ) };
		session.releaseStart();

		if ( finished ) releaseAt( m_resident.size() - 1 );
	}
}

void ScriptRunner::Worker::advance()
{
	for ( std::size_t index {}; index < m_resident.size(); )
	{
		if ( m_handles[ index ]->impl().step( *m_scripts, *m_resident[ index ] ) )
			releaseAt( index );
		else
			++index;
	}
}

void ScriptRunner::Worker::releaseAt( const std::size_t index )
{
	// Keep the session alive while realm destruction calls back into it.
	const std::shared_ptr< SessionContext > handle { m_handles[ index ] };
	handle->impl().releaseWork( *m_scripts, *m_resident[ index ] );

	m_resident.erase( m_resident.begin() + static_cast< std::ptrdiff_t >( index ) );
	m_handles.erase( m_handles.begin() + static_cast< std::ptrdiff_t >( index ) );
}

void ScriptRunner::Worker::discardAll()
{
	for ( std::size_t index {}; index < m_resident.size(); ++index )
	{
		m_handles[ index ]->impl().abandon( *m_resident[ index ] );
		m_handles[ index ]->impl().releaseWork( *m_scripts, *m_resident[ index ] );
	}

	m_resident.clear();
	m_handles.clear();

	const std::scoped_lock lock { m_mutex };
	m_inbox.clear();
}

ScriptRunner::ScriptRunner( Options options, BytecodeCache& bytecode ) : m_options( options ), m_bytecode( bytecode )
{
	if ( m_options.threads == 0 ) m_options.threads = 1;

	m_workers.reserve( m_options.threads );

	for ( std::size_t index {}; index < m_options.threads; ++index )
		m_workers.emplace_back( std::make_unique< Worker >( *this, index ) );

	spdlog::info( "downloader script: {} script threads", m_options.threads );
}

ScriptRunner::~ScriptRunner()
{
	stop();
}

void ScriptRunner::stop()
{
	if ( !m_running.exchange( false ) ) return;

	for ( const auto& worker : m_workers ) worker->notify();
	for ( const auto& worker : m_workers ) worker->join();

	std::deque< ScriptWork > queued {};

	{
		const std::scoped_lock lock { m_mutex };
		queued.swap( m_queue );
	}

	for ( ScriptWork& work : queued ) work.handle->impl().retireQueued( work.id );
}

bool ScriptRunner::submit( ScriptWork work )
{
	{
		const std::scoped_lock lock { m_mutex };

		if ( !m_running.load() || !work.handle->impl().acceptsWork() ) return false;

		m_queue.emplace_back( std::move( work ) );
	}

	for ( const auto& worker : m_workers ) worker->notify();
	return true;
}

std::optional< ScriptWork > ScriptRunner::take()
{
	const std::scoped_lock lock { m_mutex };

	for ( auto entry { m_queue.begin() }; entry != m_queue.end(); ++entry )
	{
		if ( !entry->handle->impl().reserveStart() ) continue;

		ScriptWork work { std::move( *entry ) };
		m_queue.erase( entry );
		return work;
	}

	return std::nullopt;
}

bool ScriptRunner::complete( const std::size_t worker, ScriptCompletion completion )
{
	if ( !m_running.load() || worker >= m_workers.size() ) return false;

	m_workers[ worker ]->deliver( std::move( completion ) );
	return true;
}

void ScriptRunner::discard( const SessionContext* session )
{
	std::deque< ScriptWork > dropped {};

	{
		const std::scoped_lock lock { m_mutex };

		for ( auto entry { m_queue.begin() }; entry != m_queue.end(); )
		{
			if ( entry->handle.get() != session )
			{
				++entry;
				continue;
			}

			dropped.emplace_back( std::move( *entry ) );
			entry = m_queue.erase( entry );
		}
	}

	for ( ScriptWork& work : dropped ) work.handle->impl().retireQueued( work.id );
}

} // namespace idhan::downloader
