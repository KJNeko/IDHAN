#pragma once

#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include "IDHANDownloader/SessionSnapshot.hpp"

namespace idhan::downloader
{

class SessionDiagnostics
{
  public:

	struct LoopState
	{
		std::size_t queued {};
		std::size_t in_flight_requests {};
		std::vector< WorkSnapshot > work {};
		std::vector< PendingRequestSnapshot > requests {};
	};

  private:

	static constexpr std::size_t ring_capacity { 256 };

	mutable std::mutex m_mutex {};
	SessionCounters m_counters {};
	std::deque< SessionEvent > m_events {};
	std::uint64_t m_next_sequence { 1 };
	LoopState m_loop {};

	void append( SessionEvent event );

  public:

	void recordStarted( const WorkInfo& info );
	void recordCompleted( const WorkInfo& info );
	void recordFailed( const WorkInfo& info, const std::string& error );
	void recordRequest( const RequestInfo& info );
	void recordRequestFailed( const RequestFailure& info );
	void recordImported( const ImportInfo& info );
	void recordImportFailed( WorkID work, const std::string& url, const std::string& error );
	void recordFollowed( const WorkInfo& info, FollowStatus status );
	void recordFinished();

	void publish( LoopState state );

	[[nodiscard]] SessionSnapshot snapshot( std::uint64_t since ) const;
};

} // namespace idhan::downloader
