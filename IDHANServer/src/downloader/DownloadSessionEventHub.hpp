#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "IDHANTypes.hpp"
#include "IDHANDownloader/http/DownloadSpeed.hpp"
#include "downloaderEvents.hpp"
#include "idhan/http/streaming/SSEEventHub.hpp"
#include "idhan/runtime/Task.hpp"

namespace idhan::db
{
class DBContext;
}

namespace idhan::downloader
{
class DownloadSessionEventHub
{
	struct Progress
	{
		std::uint64_t bytes {};
		std::uint64_t total {};

		bool operator==( const Progress& ) const = default;
	};

	struct Network
	{
		std::uint64_t downloading {};
		std::uint64_t waiting {};
		std::uint64_t waiting_parsers {};
		std::uint64_t waiting_requests {};
		std::uint64_t waiting_http_rate {};
		std::uint64_t waiting_http_shard {};
		std::uint64_t waiting_storage {};
		std::uint64_t downloaded_bytes {};
		std::uint64_t average_import_bytes {};

		bool operator==( const Network& ) const = default;
	};

	std::mutex m_mutex {};
	std::unordered_set< DownloadSessionID > m_active {};
	std::unordered_map< DownloadSessionID, std::uint64_t > m_generations {};
	std::unordered_map< DownloadSessionID, std::unordered_map< DownloadSessionUrlID, Progress > > m_progress {};
	std::unordered_map< DownloadSessionID, Network > m_network {};
	std::chrono::steady_clock::time_point m_speed_at { std::chrono::steady_clock::now() };
	std::unordered_map< DownloadSessionID, DownloadSpeed > m_speed_samples;
	events::Bandwidth m_bandwidth;

	void publishBandwidth();
	void publishNetwork( DownloadSessionID session_id );
	void publishProgress( DownloadSessionID session_id );

  public:

	static DownloadSessionEventHub& instance();

	//! last_used_at is sent when a root URL was just submitted.
	void started( DownloadSessionID session_id, std::optional< std::int64_t > last_used_at = std::nullopt );
	//! Publishes idle unless the session restarted meanwhile.
	void finished( DownloadSessionID session_id );
	void removed( DownloadSessionID session_id );
	//! Runs once a second: progress, network totals, and any completion that was missed.
	void flush();
	runtime::Task< void > initializeSubscription(
		db::DBContext& db,
		http::SSESubscription& subscription,
		std::vector< DownloadSessionID > ids );
};
} // namespace idhan::downloader
