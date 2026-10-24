#include "ScanProgress.hpp"

#include <algorithm>
#include <utility>

namespace idhan::scanning
{
void ScanProgress::setPhase( std::string phase )
{
	std::lock_guard lock { m_mutex };
	m_phase = std::move( phase );
	if ( m_phase == "failed" ) m_tasks.clear();
}

void ScanProgress::checkedRecord()
{
	std::lock_guard lock { m_mutex };
	++m_checked_records;
}

void ScanProgress::countedFile()
{
	std::lock_guard lock { m_mutex };
	++m_counted_files;
}

void ScanProgress::finishCounting( const bool complete )
{
	std::lock_guard lock { m_mutex };
	if ( complete ) m_total_files = m_counted_files;
}

std::uint64_t ScanProgress::beginFile( const std::filesystem::path& path )
{
	std::lock_guard lock { m_mutex };
	const auto task_id { ++m_scheduled_files };
	m_tasks.emplace( task_id, FileTask { path.string() } );
	// Directory contents can change after counting. Never report fewer files than have been admitted.
	if ( m_total_files ) m_total_files = std::max( *m_total_files, m_scheduled_files );
	return task_id;
}

void ScanProgress::setStage( const std::uint64_t task_id, std::string stage )
{
	std::lock_guard lock { m_mutex };
	m_tasks.at( task_id ).stage = std::move( stage );
}

void ScanProgress::finishFile( const std::uint64_t task_id, const std::uint64_t bytes )
{
	std::lock_guard lock { m_mutex };
	m_tasks.erase( task_id );
	++m_processed_files;
	m_processed_bytes += bytes;
}

void ScanProgress::recordFailure( const std::filesystem::path& path, const std::string& message )
{
	std::lock_guard lock { m_mutex };
	++m_failed_count;
	m_last_failure = ScanFailure { path, message };
}

void ScanProgress::finishScan( const bool stopped )
{
	std::lock_guard lock { m_mutex };
	m_phase = stopped ? "stopped" : "completed";
	// After traversal finishes the processed count is authoritative, including files added or removed mid-scan.
	if ( !stopped ) m_total_files = m_processed_files;
}

Json::Value ScanProgress::snapshot() const
{
	std::lock_guard lock { m_mutex };
	Json::Value result { Json::objectValue };
	result[ "kind" ] = "cluster_scan";
	result[ "phase" ] = m_phase;
	result[ "checked_records" ] = static_cast< Json::UInt64 >( m_checked_records );
	result[ "counted_files" ] = static_cast< Json::UInt64 >( m_counted_files );
	result[ "processed_files" ] = static_cast< Json::UInt64 >( m_processed_files );
	result[ "processed_bytes" ] = static_cast< Json::UInt64 >( m_processed_bytes );
	result[ "failed_count" ] = static_cast< Json::UInt64 >( m_failed_count );
	result[ "active_files" ] = static_cast< Json::UInt64 >( m_tasks.size() );
	if ( m_total_files )
	{
		result[ "total_files" ] = static_cast< Json::UInt64 >( *m_total_files );
		result[ "remaining_files" ] = static_cast< Json::UInt64 >( *m_total_files - m_processed_files );
	}
	if ( m_last_failure )
	{
		result[ "last_failure" ][ "path" ] = m_last_failure->path.string();
		result[ "last_failure" ][ "error" ] = m_last_failure->message;
	}
	Json::Value tasks { Json::arrayValue };
	for ( const auto& [ id, task ] : m_tasks )
	{
		Json::Value entry { Json::objectValue };
		entry[ "task_id" ] = static_cast< Json::UInt64 >( id );
		entry[ "path" ] = task.path;
		entry[ "stage" ] = task.stage;
		tasks.append( std::move( entry ) );
	}
	result[ "tasks" ] = std::move( tasks );
	return result;
}
} // namespace idhan::scanning
