#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "IDHANDownloader/cookies/CookiePersistence.hpp"
#include "IDHANDownloader/DownloaderConfig.hpp"
#include "IDHANDownloader/DownloaderHost.hpp"
#include "IDHANDownloader/imports/ImportSinkFactory.hpp"
#include "IDHANDownloader/http/LaneObserver.hpp"
#include "IDHANDownloader/parsers/ParserUpdate.hpp"
#include "IDHANDownloader/parsers/ParserCacheSnapshot.hpp"
#include "IDHANDownloader/secrets/SecretProvider.hpp"
#include "IDHANDownloader/session/SessionContext.hpp"
#include "IDHANDownloader/session/SessionSnapshot.hpp"
#include "IDHANDownloader/urls/UrlNormalization.hpp"

namespace idhan::downloader
{

class DownloaderContext
{
	class Impl;
	std::unique_ptr< Impl > m_impl;

	explicit DownloaderContext( std::unique_ptr< Impl > impl );

  public:

	[[nodiscard]] static folly::coro::Task< std::expected< std::unique_ptr< DownloaderContext >, std::string > > create(
		DownloaderConfig config,
		DownloaderHost host );

	DownloaderContext( const DownloaderContext& ) = delete;
	DownloaderContext& operator=( const DownloaderContext& ) = delete;
	~DownloaderContext();

	//! Sessions must be destroyed before their context.
	[[nodiscard]] std::shared_ptr< SessionContext > createSession( SessionOptions options );

	//! Returns an error when the URL is invalid or no URL class accepts it.
	[[nodiscard]] std::expected< void, std::string > validate( std::string_view url ) const;

	[[nodiscard]] std::expected< NormalizedUrl, std::string > normalizeUrl( std::string_view url ) const;

	[[nodiscard]] std::expected< ParserUpdateReport, std::string > updateParsers();

	//! Relative paths are resolved within parser_directory; files must already be installed there.
	[[nodiscard]] std::expected< ParserUpdateReport, std::string > updateParser( const std::filesystem::path& path );
	[[nodiscard]] ParserUpdateReport parserStatus() const;
	[[nodiscard]] Json::Value downloadOptions() const;
	[[nodiscard]] Json::Value parsers() const;
	[[nodiscard]] std::expected< void, std::string > installParser(
		std::string filename,
		std::string source,
		bool replace );
	[[nodiscard]] std::expected< std::string, std::string > parserSource( std::string filename ) const;
	[[nodiscard]] std::expected< void, std::string > setParserHost( std::string host, std::string parser );
	void removeParserHost( std::string_view host );

	//! Backoff persists until explicitly cleared by either reset function.
	void resetBackoff( std::string_view lane_key );
	void resetAllBackoff();

	[[nodiscard]] std::vector< LaneSnapshot > laneSnapshots() const;
	[[nodiscard]] ParserCacheSnapshot cacheSnapshot() const;

	//! Returns live sessions in creation order.
	[[nodiscard]] std::vector< SessionSnapshot > sessionSnapshots( std::uint64_t since = 0 ) const;

	//! Idempotently stops sessions and lanes, drains host writes, and aborts partial
	//! transfers without failure notifications. Hosts can resume their unfinished rows.
	folly::coro::Task< void > shutdown();
};

} // namespace idhan::downloader
