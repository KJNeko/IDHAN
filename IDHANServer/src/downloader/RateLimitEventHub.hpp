#pragma once

#include <IDHANDownloader/LaneObserver.hpp>

#include <chrono>
#include <mutex>
#include <unordered_map>

#include "idhan/http/SSEEventHub.hpp"

namespace idhan::downloader
{

class RateLimitEventHub final : public LaneObserver
{
	struct StoredSnapshot
	{
		LaneSnapshot snapshot {};
		std::chrono::steady_clock::time_point captured_at {};
	};

	std::mutex m_mutex {};
	std::unordered_map< std::string, StoredSnapshot > m_limits {};
	http::SSEEventHub m_events {};

  public:

	static RateLimitEventHub& instance();
	[[nodiscard]] http::SSESubscription subscribe();
	void shutdown();

	void onLaneChanged( const LaneSnapshot& snapshot ) override;
};

} // namespace idhan::downloader
