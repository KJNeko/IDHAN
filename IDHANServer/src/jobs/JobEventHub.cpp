#include "JobEventHub.hpp"

#include <json/writer.h>

#include <chrono>
#include <utility>

#include "JobTaskStatus.hpp"
#include "logging/log.hpp"

namespace idhan::jobs
{
static http::ValidatedJson eventJson( const Json::Value& value )
{
	Json::StreamWriterBuilder builder;
	builder[ "indentation" ] = "";
	return http::ValidatedJson::parse( Json::writeString( builder, value ) ).value();
}

JobEventHub::JobEventHub() : m_publisher( [ this ]( const std::stop_token stop ) { run( stop ); } )
{}

JobEventHub::~JobEventHub()
{
	m_publisher.request_stop();
	m_wakeup.notify_all();
	if ( m_publisher.joinable() ) m_publisher.join();
	m_events.shutdown();
}

void JobEventHub::shutdown()
{
	m_events.shutdown();
}

void JobEventHub::changed( const JobID id )
{
	std::lock_guard lock { m_mutex };
	if ( !m_jobs.contains( id ) ) return;

	m_dirty.insert( id );
	m_wakeup.notify_one();
}

void JobEventHub::add( const std::shared_ptr< JobTaskStatus >& status )
{
	const auto id { status->m_id };
	status->setChangeCallback(
		[ weak = weak_from_this(), id ]
		{
			const auto hub { weak.lock() };
			if ( hub ) hub->changed( id );
		} );
	std::lock_guard lock { m_mutex };
	m_jobs[ id ] = status;
	m_dirty.insert( id );
	m_wakeup.notify_one();
}

void JobEventHub::remove( const JobID id )
{
	std::lock_guard lock { m_mutex };
	m_jobs.erase( id );
	m_snapshots.erase( id );
	m_dirty.erase( id );
	Json::Value payload;
	payload[ "job_id" ] = id;
	payload[ "removed" ] = true;
	m_events.remove( std::to_string( id ), eventJson( payload ) );
}

http::SSESubscription JobEventHub::subscribe()
{
	std::lock_guard lock { m_mutex };
	Json::Value payload;
	payload[ "jobs" ] = Json::Value { Json::arrayValue };
	for ( const auto& [ id, snapshot ] : m_snapshots ) payload[ "jobs" ].append( snapshot );
	return m_events.subscribe( {}, { eventJson( payload ) }, false );
}

void JobEventHub::run( const std::stop_token stop )
{
	std::unique_lock lock { m_mutex };
	while ( !stop.stop_requested() )
	{
		m_wakeup.wait( lock, stop, [ this ] { return !m_dirty.empty(); } );
		if ( stop.stop_requested() ) break;

		m_wakeup.wait_for( lock, stop, std::chrono::milliseconds { 100 }, [] { return false; } );
		auto dirty { std::exchange( m_dirty, {} ) };
		for ( const auto id : dirty )
		{
			const auto found { m_jobs.find( id ) };
			if ( found == m_jobs.end() ) continue;

			const auto status { found->second.lock() };
			if ( !status ) continue;

			lock.unlock();
			try
			{
				auto snapshot { status->snapshot() };
				auto encoded { eventJson( snapshot ) };
				lock.lock();
				if ( !m_jobs.contains( id ) ) continue;

				m_snapshots[ id ] = std::move( snapshot );
				m_events.publish( std::to_string( id ), std::move( encoded ) );
			}
			catch ( const std::exception& error )
			{
				if ( !lock.owns_lock() ) lock.lock();
				log::warn( "Could not publish job {} progress: {}", id, error.what() );
			}
		}
	}
}
} // namespace idhan::jobs
