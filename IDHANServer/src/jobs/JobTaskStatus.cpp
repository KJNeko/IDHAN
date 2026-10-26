#include "JobTaskStatus.hpp"

#include <format>
#include <utility>

namespace idhan::jobs
{
static std::optional< std::int64_t > epochSeconds( const std::chrono::steady_clock::time_point point )
{
	if ( point == std::chrono::steady_clock::time_point {} ) return std::nullopt;

	const auto elapsed { std::chrono::steady_clock::now() - point };
	const auto time { std::chrono::system_clock::now() - elapsed };
	return std::chrono::duration_cast< std::chrono::seconds >( time.time_since_epoch() ).count();
}
} // namespace idhan::jobs

void JobTaskStatus::setChangeCallback( std::function< void() > callback )
{
	std::lock_guard lock { m_snapshot_mutex };
	m_on_change = std::move( callback );
}

void JobTaskStatus::notifyChanged() const
{
	std::lock_guard lock { m_snapshot_mutex };
	if ( m_on_change ) m_on_change();
}

Json::Value JobTaskStatus::snapshot() const
{
	Json::Value result;
	result[ "job_id" ] = m_id;
	result[ "job_name" ] = m_function_name;
	result[ "continuous" ] = m_continuous;
	result[ "location" ] = std::format( "{}:{}:{}", m_location.file_name(), m_location.line(), m_location.column() );
	const auto start { idhan::jobs::epochSeconds( m_start_time.load() ) };
	if ( start ) result[ "start_time" ] = *start;
	const bool done { m_done.load( std::memory_order_acquire ) };
	result[ "completed" ] = done;
	result[ "status" ] = !done ? "running" : m_failed.load() ? "failed" : "completed";
	if ( done )
	{
		const auto end { idhan::jobs::epochSeconds( m_completion_time ) };
		if ( end ) result[ "end_time" ] = *end;
		if ( m_failed.load() ) result[ "error" ] = m_error_message;
	}
	const auto progress { progressSnapshot() };
	if ( progress ) result[ "progress" ] = *progress;
	const auto response { responseSnapshot() };
	if ( response ) result[ "response" ] = *response;
	return result;
}

void JobTaskStatus::setResponse( Json::Value response )
{
	{
		std::lock_guard lock { m_snapshot_mutex };
		m_response = std::move( response );
	}
	notifyChanged();
}

std::optional< Json::Value > JobTaskStatus::responseSnapshot() const
{
	std::lock_guard lock { m_snapshot_mutex };
	return m_response;
}

void JobTaskStatus::setProgress( std::shared_ptr< const JobProgress > progress )
{
	{
		std::lock_guard lock { m_snapshot_mutex };
		progress->setChangeCallback( m_on_change );
		m_progress = std::move( progress );
	}
	notifyChanged();
}

std::optional< Json::Value > JobTaskStatus::progressSnapshot() const
{
	std::shared_ptr< const JobProgress > progress;
	{
		std::lock_guard lock { m_snapshot_mutex };
		progress = m_progress;
	}
	if ( !progress ) return std::nullopt;

	return progress->snapshot();
}
