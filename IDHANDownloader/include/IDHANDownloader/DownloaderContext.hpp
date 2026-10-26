#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "CookiePersistence.hpp"
#include "DownloaderConfig.hpp"
#include "DownloaderHost.hpp"
#include "ImportSinkFactory.hpp"
#include "LaneObserver.hpp"
#include "SecretProvider.hpp"
#include "SessionContext.hpp"
#include "SessionSnapshot.hpp"

namespace idhan::downloader
{

class DownloaderContext
{
	class Impl;
	std::unique_ptr< Impl > m_impl;

	explicit DownloaderContext( std::unique_ptr< Impl > impl );

  public:

	[[nodiscard]] static std::expected< std::unique_ptr< DownloaderContext >, std::string > create(
		DownloaderConfig config,
		DownloaderHost host );

	DownloaderContext( const DownloaderContext& ) = delete;
	DownloaderContext& operator=( const DownloaderContext& ) = delete;
	~DownloaderContext();

	//! Sessions must be destroyed before their context.
	[[nodiscard]] std::shared_ptr< SessionContext > createSession( SessionOptions options );

	//! Returns an error when the URL is invalid or no URL class accepts it.
	[[nodiscard]] std::expected< void, std::string > validate( std::string_view url ) const;

	// Maintenance only sorts URLs accepted by a configured class with sorting enabled.
	[[nodiscard]] std::string classStorageUrl( std::string_view url ) const;

	//! Backoff persists until explicitly cleared by either reset function.
	void resetBackoff( std::string_view lane_key );
	void resetAllBackoff();

	[[nodiscard]] std::vector< LaneSnapshot > laneSnapshots() const;

	//! Returns live sessions in creation order.
	[[nodiscard]] std::vector< SessionSnapshot > sessionSnapshots( std::uint64_t since = 0 ) const;

	//! Idempotently stops sessions and lanes.
	void shutdown();
};

} // namespace idhan::downloader
