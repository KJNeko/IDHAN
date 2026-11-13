#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>

#include "IDHANDownloader/http/DownloadSpeed.hpp"
#include "IDHANDownloader/http/RequestTiming.hpp"
#include "IDHANDownloader/http/TransferEvent.hpp"

namespace idhan::downloader
{
class NetworkActivity
{
  public:

	enum class Phase
	{
		WAITING,
		DOWNLOADING,
		STORAGE,
		FINISHED
	};

	struct Request
	{
		std::atomic< Phase > phase { Phase::WAITING };
		//! Body bytes received by the current hop.
		std::atomic< std::uint64_t > received {};
		//! Content-Length of the current hop, or 0 when the server did not send one.
		std::atomic< std::uint64_t > expected {};
		//! Assigned before the request is sent and never replaced afterwards.
		std::function< void( TransferEvent ) > listener {};
		std::atomic_bool announced {};
		std::atomic_bool storing {};
		std::atomic_bool finished {};
		//! Steady-clock ticks of the current attempt's milestones; 0 until reached.
		std::atomic< std::chrono::steady_clock::rep > queued_at {};
		std::atomic< std::chrono::steady_clock::rep > sent_at {};
		std::atomic< std::chrono::system_clock::rep > sent_wall_at {};
		std::atomic< std::chrono::steady_clock::rep > received_at {};
		std::atomic< std::uint32_t > attempts {};

		[[nodiscard]] static std::chrono::steady_clock::rep ticksNow()
		{
			return std::chrono::steady_clock::now().time_since_epoch().count();
		}

		[[nodiscard]] static std::optional< std::chrono::steady_clock::time_point > fromTicks(
			const std::chrono::steady_clock::rep ticks )
		{
			if ( ticks == 0 ) return std::nullopt;

			return std::chrono::steady_clock::time_point { std::chrono::steady_clock::duration { ticks } };
		}

		void stampQueued()
		{
			sent_at = 0;
			sent_wall_at = 0;
			received_at = 0;
			queued_at = ticksNow();
			++attempts;
		}

		void stampSent()
		{
			sent_wall_at = std::chrono::system_clock::now().time_since_epoch().count();
			sent_at = ticksNow();
		}

		void stampReceived() { received_at = ticksNow(); }

		[[nodiscard]] RequestTiming timing( const std::chrono::steady_clock::time_point made ) const
		{
			const auto wall { sent_wall_at.load() };
			return RequestTiming {
				.made = made,
				.queued = fromTicks( queued_at.load() ),
				.sent = fromTicks( sent_at.load() ),
				.sent_wall = wall == 0 ? std::nullopt :
				                         std::optional { std::chrono::system_clock::time_point {
											 std::chrono::system_clock::duration { wall } } },
				.received = fromTicks( received_at.load() ),
				.attempts = attempts.load()
			};
		}

		//! Every hop and retry dispatches again; only the first dispatch is reported.
		void beginDownloading()
		{
			if ( !listener ) return;

			if ( announced.exchange( true ) ) return;

			listener( TransferEvent::DOWNLOADING );
		}

		void beginStoring()
		{
			if ( !listener ) return;

			if ( storing.exchange( true ) ) return;

			listener( TransferEvent::STORING );
		}

		//! Reported only for requests that were announced, so listeners can pair it with DOWNLOADING.
		void finish()
		{
			if ( !listener ) return;

			if ( !announced.load() ) return;

			if ( finished.exchange( true ) ) return;

			listener( TransferEvent::FINISHED );
		}
	};

  private:

	mutable std::mutex m_mutex {};
	mutable DownloadSpeed m_download_speed;
	std::uint64_t m_downloaded_bytes {};

  public:

	void received( const std::size_t bytes )
	{
		const std::scoped_lock lock { m_mutex };
		m_downloaded_bytes += bytes;
	}

	[[nodiscard]] std::uint64_t total() const
	{
		const std::scoped_lock lock { m_mutex };
		return m_downloaded_bytes;
	}

	[[nodiscard]] std::uint64_t speed() const
	{
		const std::scoped_lock lock { m_mutex };
		return m_download_speed.sample( m_downloaded_bytes );
	}
};
} // namespace idhan::downloader
