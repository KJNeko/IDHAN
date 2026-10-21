#pragma once

#include <IDHANDownloader/SessionObserver.hpp>

#include <mutex>
#include <unordered_map>

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
	void onRequestFailed( const RequestFailure& failure ) override;
	std::expected< void, std::string > onImportQueued( const WorkInfo& info ) override;
	void onImported( const ImportInfo& info ) override;
	void onImportFailed( const WorkInfo& info, const std::string& url, const std::string& error ) override;
	void onFollowed( const WorkInfo& info, FollowStatus status ) override;
	std::optional< std::int64_t > alreadyImported( const std::string& url ) override;
};

} // namespace idhan::downloader
