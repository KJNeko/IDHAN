#include "JobTaskStatus.hpp"

#include <utility>

void JobTaskStatus::setResponse( Json::Value response )
{
	std::lock_guard lock { m_snapshot_mutex };
	m_response = std::move( response );
}

std::optional< Json::Value > JobTaskStatus::responseSnapshot() const
{
	std::lock_guard lock { m_snapshot_mutex };
	return m_response;
}

void JobTaskStatus::setProgress( std::shared_ptr< const JobProgress > progress )
{
	std::lock_guard lock { m_snapshot_mutex };
	m_progress = std::move( progress );
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
