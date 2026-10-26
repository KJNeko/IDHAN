#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <string>

#include "clusterScan.hpp"
#include "jobs/JobProgress.hpp"

namespace idhan::scanning
{
class ScanProgress final : public JobProgress
{
  public:

	void setPhase( std::string phase );
	void checkedRecord();
	void countedFile();
	void finishCounting( bool complete );
	[[nodiscard]] std::uint64_t beginFile( const std::filesystem::path& path );
	void setStage( std::uint64_t task_id, std::string stage );
	void finishFile( std::uint64_t task_id, std::uint64_t bytes );
	void finishScan( bool stopped );
	void recordFailure( const std::filesystem::path& path, const std::string& message );
	[[nodiscard]] Json::Value snapshot() const override;

  private:

	struct FileTask
	{
		std::string path;
		std::string stage { "checking_record" };
	};

	mutable std::mutex m_mutex {};
	std::string m_phase { "verifying_cluster" };
	std::uint64_t m_checked_records {};
	std::uint64_t m_counted_files {};
	std::optional< std::uint64_t > m_total_files {};
	std::uint64_t m_scheduled_files {};
	std::uint64_t m_processed_files {};
	std::uint64_t m_processed_bytes {};
	std::uint64_t m_failed_count {};
	std::map< std::uint64_t, FileTask > m_tasks {};
	std::optional< ScanFailure > m_last_failure {};
};
} // namespace idhan::scanning
