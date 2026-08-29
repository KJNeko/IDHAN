#pragma once

#include <IDHANDownloader/DownloaderContext.hpp>
#include <IDHANDownloader/ImportSink.hpp>
#include <IDHANDownloader/SecretProvider.hpp>
#include <IDHANDownloader/SessionObserver.hpp>
#include <IDHANDownloader/SessionSnapshot.hpp>
#include <drogon/drogon.h>

#include <atomic>
#include <expected>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

#include "CookiePersistence.hpp"
#include "IDHANTypes.hpp"

namespace idhan::downloader
{
class DownloadSessionManager;

class SessionRowObserver final : public SessionObserver
{
	DownloadSessionManager& m_manager;
	DownloadSessionID m_session_id {};
	mutable std::mutex m_mutex {};
	std::unordered_map< WorkID, DownloadSessionUrlID > m_rows {};

  public:

	SessionRowObserver( DownloadSessionManager& manager, DownloadSessionID session_id );

	[[nodiscard]] DownloadSessionUrlID rowFor( WorkID work ) const;
	[[nodiscard]] std::unordered_map< WorkID, DownloadSessionUrlID > rows() const;

	void adopt( WorkID work, DownloadSessionUrlID row );
	void forget( WorkID work );

	void onStarted( const WorkInfo& info ) override;
	void onCompleted( const WorkInfo& info ) override;
	void onFailed( const WorkInfo& info, const std::string& error ) override;
	void onImported( const ImportInfo& info ) override;
	void onImportFailed( const WorkInfo& info, const std::string& url, const std::string& error ) override;
	void onFollowed( const WorkInfo& info, FollowStatus status ) override;
	std::optional< std::int64_t > alreadyImported( const std::string& url ) override;
};

class DownloadSessionManager final
{
	struct Session
	{
		DownloadSessionID id {};
		std::unique_ptr< SessionRowObserver > observer {};
		std::shared_ptr< SessionContext > context {};
	};

	class Imports;
	class Secrets;

	mutable std::mutex m_mutex {};
	std::unique_ptr< DownloaderContext > m_downloader {};
	std::unique_ptr< Imports > m_imports {};
	std::unique_ptr< Secrets > m_secrets {};
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

	drogon::Task< void > restore( const drogon::orm::DbClientPtr& db );
	[[nodiscard]] std::expected< void, std::string > submit(
		DownloadSessionUrlID job_id,
		DownloadSessionID session_id,
		std::string url );
	void destroy( DownloadSessionID session_id );
	void shutdown();

	[[nodiscard]] std::expected< void, std::string > resetBackoff( std::string_view lane_key );
	[[nodiscard]] std::vector< LaneSnapshot > laneSnapshots();
	[[nodiscard]] drogon::Task< std::expected< std::unordered_map< std::string, std::string >, std::string > >
		secrets();
	[[nodiscard]] drogon::Task< std::expected< void, std::string > > setSecrets(
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
};

DownloadSessionManager& downloadSessionManager();
} // namespace idhan::downloader
