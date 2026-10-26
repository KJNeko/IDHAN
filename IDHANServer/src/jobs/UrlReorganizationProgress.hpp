#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <utility>

#include "JobProgress.hpp"

namespace idhan::jobs
{
class UrlReorganizationProgress final : public JobProgress
{
	mutable std::mutex m_mutex {};
	Json::Value m_state { Json::objectValue };

  public:

	UrlReorganizationProgress()
	{
		m_state[ "kind" ] = "url_reorganization";
		m_state[ "phase" ] = "starting";
		m_state[ "message" ] = "Loading URL class rules";
		m_state[ "processed_count" ] = Json::UInt64 { 0 };
		m_state[ "sorted_count" ] = Json::UInt64 { 0 };
		m_state[ "merged_count" ] = Json::UInt64 { 0 };
		m_state[ "mappings_processed_count" ] = Json::UInt64 { 0 };
		m_state[ "committed_batch_count" ] = Json::UInt64 { 0 };
	}

	void setPhase( std::string phase, std::string message )
	{
		std::lock_guard lock { m_mutex };
		m_state[ "phase" ] = std::move( phase );
		m_state[ "message" ] = std::move( message );
		changed();
	}

	void scanned( const std::uint64_t processed, const std::uint64_t sorted )
	{
		std::lock_guard lock { m_mutex };
		m_state[ "processed_count" ] = static_cast< Json::UInt64 >( processed );
		m_state[ "sorted_count" ] = static_cast< Json::UInt64 >( sorted );
		changed();
	}

	void planned( const std::uint64_t total, const std::uint64_t unique, const std::uint64_t merges )
	{
		std::lock_guard lock { m_mutex };
		m_state[ "total_count" ] = static_cast< Json::UInt64 >( total );
		m_state[ "processed_count" ] = static_cast< Json::UInt64 >( total );
		m_state[ "unique_count" ] = static_cast< Json::UInt64 >( unique );
		m_state[ "merge_candidate_count" ] = static_cast< Json::UInt64 >( merges );
		changed();
	}

	void batchCommitted( const std::uint64_t merges, const std::uint64_t mappings )
	{
		std::lock_guard lock { m_mutex };
		m_state[ "merged_count" ] = m_state[ "merged_count" ].asUInt64() + merges;
		m_state[ "mappings_processed_count" ] = m_state[ "mappings_processed_count" ].asUInt64() + mappings;
		m_state[ "committed_batch_count" ] = m_state[ "committed_batch_count" ].asUInt64() + 1;
		changed();
	}

	// Finish after all batches succeed; batchCommitted publishes partial committed work.
	void committed( const std::uint64_t merges )
	{
		std::lock_guard lock { m_mutex };
		m_state[ "merged_count" ] = static_cast< Json::UInt64 >( merges );
		m_state[ "phase" ] = "completed";
		m_state[ "message" ] = "URL reorganization complete";
		changed();
	}

	[[nodiscard]] Json::Value snapshot() const override
	{
		std::lock_guard lock { m_mutex };
		return m_state;
	}
};
} // namespace idhan::jobs
