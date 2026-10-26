#pragma once

#include <IDHANDownloader/LaneObserver.hpp>

#include <chrono>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "idhan/http/SSEEventHub.hpp"

namespace idhan::downloader
{

class RateLimitEventHub final : public LaneObserver
{
  private:

	struct StoredSnapshot
	{
		LaneSnapshot snapshot {};
		std::chrono::steady_clock::time_point captured_at {};
	};

	std::mutex m_mutex {};
	std::unordered_map< std::string, StoredSnapshot > m_limits {};

  public:

	static RateLimitEventHub& instance();
	[[nodiscard]] std::vector< http::SSEInitialEvent > snapshotEvents();
	void onLaneChanged( const LaneSnapshot& snapshot ) override;
};

} // namespace idhan::downloader
