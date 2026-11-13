#pragma once

#include <IDHANDownloader/DownloaderContext.hpp>

#include <atomic>
#include <folly/coro/Mutex.h>
#include <expected>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "CookiePersistence.hpp"
#include "IDHANTypes.hpp"
#include "idhan/runtime/Task.hpp"

namespace idhan::db
{
class DBContext;
}

namespace idhan::downloader
{
class SessionRowObserver;
class DownloadImports;

//! What one URL's in-flight requests have received so far.
struct UrlTransfer
{
	DownloadSessionUrlID url_id {};
	std::uint64_t bytes {};
	//! 0 when any of the requests has no known size.
	std::uint64_t total {};
};
class DatabaseSecrets;
class DatabaseParserHosts;

class DownloadSessionManager final
{
	struct Session
	{
		DownloadSessionID id {};
		std::unique_ptr< SessionRowObserver > observer {};
		std::shared_ptr< SessionContext > context {};
	};

	mutable std::mutex m_mutex {};
	folly::coro::Mutex m_initialize_mutex {};
	folly::coro::Mutex m_shutdown_mutex {};
	folly::coro::Mutex m_session_creation_mutex {};
	std::unique_ptr< DownloaderContext > m_downloader {};
	std::unique_ptr< DownloadImports > m_imports {};
	std::shared_ptr< DatabaseSecrets > m_secrets {};
	std::unique_ptr< DatabaseCookies > m_cookies {};
	std::unique_ptr< DatabaseParserHosts > m_parser_hosts {};
	std::unordered_map< DownloadSessionID, std::shared_ptr< Session > > m_sessions {};
	std::atomic_bool m_stopped {};

	[[nodiscard]] runtime::Task< std::expected< void, std::string > > initialize();
	[[nodiscard]] runtime::Task< std::shared_ptr< Session > > sessionFor(
		DownloadSessionID session_id,
		Json::Value settings );

  public:

	DownloadSessionManager() = default;
	DownloadSessionManager( const DownloadSessionManager& ) = delete;
	DownloadSessionManager& operator=( const DownloadSessionManager& ) = delete;
	~DownloadSessionManager();

	// Startup preparation awaits persisted state before accepting HTTP requests. No persisted job is created.
	[[nodiscard]] runtime::Task< std::expected< ParserUpdateReport, std::string > > prepareParsers();
	[[nodiscard]] runtime::Task< std::expected< ParserUpdateReport, std::string > > updateParsers();
	[[nodiscard]] runtime::Task< std::expected< ParserUpdateReport, std::string > > updateParser(
		std::filesystem::path path );

	// Row persistence in SessionRowObserver reaches the database through this rather than a
	// threaded-through parameter, since the downloader's SessionObserver interface has no room for one.
	[[nodiscard]] static db::DBContext& dbContext();

	struct ResumableRoot
	{
		DownloadSessionUrlID url_id {};
		DownloadSessionID session_id {};
		std::string url {};
		std::string settings {};
	};

	//! Finds unfinished roots without discarding their saved URL rows.
	[[nodiscard]] runtime::Task< std::vector< ResumableRoot > > resumable( db::DBContext& db );

	//! Records a root that could not be handed back to the downloader.
	[[nodiscard]] runtime::Task< void > markUnresumable( ResumableRoot root, std::string error, db::DBContext& db );
	//! last_used_at is announced with the session's activity when a user just submitted the URL.
	[[nodiscard]] runtime::Task< std::expected< void, std::string > > submit(
		DownloadSessionUrlID url_id,
		DownloadSessionID session_id,
		std::string url,
		std::string settings = "{}",
		std::optional< std::int64_t > last_used_at = std::nullopt,
		bool recovering = false );
	[[nodiscard]] runtime::Task< std::expected< Json::Value, std::string > > downloadOptions();
	[[nodiscard]] runtime::Task< std::expected< Json::Value, std::string > > parsers();
	[[nodiscard]] runtime::Task< std::expected< void, std::string > > installParser(
		std::string filename,
		std::string source,
		bool replace );
	[[nodiscard]] runtime::Task< std::expected< std::string, std::string > > parserSource( std::string filename );
	[[nodiscard]] static runtime::Task< std::string > sessionSettings(
		db::DBContext& db,
		DownloadSessionID session_id );
	//! The outer error means the URL classes are unavailable; each inner error is an invalid URL.
	[[nodiscard]] runtime::Task<
		std::expected< std::vector< std::expected< NormalizedUrl, std::string > >, std::string > >
		normalizeUrls( std::vector< std::string > urls );
	[[nodiscard]] runtime::Task< std::expected< void, std::string > > setParserHost(
		std::string host,
		std::string parser );
	void removeParserHost( std::string_view host );
	[[nodiscard]] bool idle( DownloadSessionID session_id );
	[[nodiscard]] SessionSnapshot networkSnapshot( DownloadSessionID session_id );
	[[nodiscard]] std::vector< std::pair< DownloadSessionID, std::uint64_t > > downloadedBytes();
	//! Groups the snapshot's in-flight requests by the URL row they belong to.
	[[nodiscard]] std::vector< UrlTransfer > transfers( DownloadSessionID session_id, const SessionSnapshot& snapshot );
	[[nodiscard]] std::unordered_map< DownloadSessionUrlID, std::string > liveStates( DownloadSessionID session_id );
	runtime::Task< void > destroy( DownloadSessionID session_id );
	runtime::Task< void > shutdown();

	[[nodiscard]] runtime::Task< std::expected< void, std::string > > resetBackoff( std::string lane_key );
	[[nodiscard]] std::vector< LaneSnapshot > laneSnapshots();
	[[nodiscard]] ParserCacheSnapshot cacheSnapshot();
	[[nodiscard]] runtime::Task< std::expected< std::unordered_map< std::string, std::string >, std::string > >
		secrets();
	[[nodiscard]] runtime::Task< std::expected< void, std::string > > setSecrets(
		std::unordered_map< std::string, std::string > values );

};

DownloadSessionManager& downloadSessionManager();
} // namespace idhan::downloader
