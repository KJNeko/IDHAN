#pragma once

#include <IDHANDownloader/DownloaderContext.hpp>
#include <IDHANDownloader/SessionSnapshot.hpp>

#include <atomic>
#include <expected>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

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
class DatabaseSecrets;

class DownloadSessionManager final
{
	struct Session
	{
		DownloadSessionID id {};
		std::unique_ptr< SessionRowObserver > observer {};
		std::shared_ptr< SessionContext > context {};
	};

	mutable std::mutex m_mutex {};
	std::unique_ptr< DownloaderContext > m_downloader {};
	std::unique_ptr< DownloadImports > m_imports {};
	std::unique_ptr< DatabaseSecrets > m_secrets {};
	std::unique_ptr< DatabaseCookies > m_cookies {};
	std::unordered_map< DownloadSessionID, std::shared_ptr< Session > > m_sessions {};
	std::string m_tag_domain { "default" };
	std::atomic_bool m_stopped {};

	[[nodiscard]] std::expected< void, std::string > initialize();
	[[nodiscard]] std::shared_ptr< Session > sessionFor( DownloadSessionID session_id );

  public:

	DownloadSessionManager() = default;
	DownloadSessionManager( const DownloadSessionManager& ) = delete;
	DownloadSessionManager& operator=( const DownloadSessionManager& ) = delete;
	~DownloadSessionManager();

	// Row persistence (addRow/markRow/addError, and SessionRowObserver::onImported) reaches the
	// database through this rather than a threaded-through parameter, since the downloader's
	// SessionObserver callback interface has no room for one.
	[[nodiscard]] static db::DBContext& dbContext();

	struct ResumableRoot
	{
		DownloadSessionUrlID job_id {};
		DownloadSessionID session_id {};
		std::string url {};
	};

	//! Resets URLs a shutdown left unfinished and reports the roots to resubmit. Submitting is left
	//! to the caller, which must not be on the database executor: submit() reaches downloader host
	//! callbacks that block on their own queries, and those resume on that executor's single thread.
	[[nodiscard]] runtime::Task< std::vector< ResumableRoot > > resumable( db::DBContext& db );

	//! Records a root that could not be handed back to the downloader.
	[[nodiscard]] runtime::Task< void > markUnresumable(
		DownloadSessionUrlID job_id,
		std::string error,
		db::DBContext& db );
	[[nodiscard]] std::expected< void, std::string > submit(
		DownloadSessionUrlID job_id,
		DownloadSessionID session_id,
		std::string url );
	[[nodiscard]] std::expected< std::vector< std::string >, std::string > classStorageUrls(
		std::vector< std::string > urls );
	void destroy( DownloadSessionID session_id );
	void shutdown();

	[[nodiscard]] std::expected< void, std::string > resetBackoff( std::string_view lane_key );
	[[nodiscard]] std::vector< LaneSnapshot > laneSnapshots();
	[[nodiscard]] runtime::Task< std::expected< std::unordered_map< std::string, std::string >, std::string > >
		secrets();
	[[nodiscard]] runtime::Task< std::expected< void, std::string > > setSecrets(
		std::unordered_map< std::string, std::string > values );

	struct SessionDebugInfo
	{
		DownloadSessionID id {};
		SessionSnapshot snapshot {};
		std::unordered_map< WorkID, DownloadSessionUrlID > rows {};
	};

	[[nodiscard]] std::vector< SessionDebugInfo > debugSnapshots(
		const std::unordered_map< DownloadSessionID, std::uint64_t >& cursors );

	[[nodiscard]] const std::string& tagDomain() const { return m_tag_domain; }

	static DownloadSessionUrlID addRow(
		DownloadSessionID session_id,
		DownloadSessionUrlID parent_row,
		const std::string& url,
		std::string_view state,
		std::string message );
	static void markRow(
		DownloadSessionID session_id,
		DownloadSessionUrlID row_id,
		std::string_view state,
		std::string error );
	static void addError(
		DownloadSessionID session_id,
		DownloadSessionUrlID row_id,
		const std::string& url,
		const std::string& lane,
		std::optional< std::int32_t > status,
		std::string message );
};

DownloadSessionManager& downloadSessionManager();
} // namespace idhan::downloader
