#pragma once

#include <IDHANDownloader/session/SessionObserver.hpp>

#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "downloaderEvents.hpp"
#include "sessionRows.hpp"

namespace idhan::downloader
{

class SessionRowObserver final : public SessionObserver
{
	struct LiveUrl
	{
		std::string state { "pending" };
		std::size_t downloads {};
		//! Set once a final state is being stored; later transfer milestones are ignored.
		bool terminal {};
	};

	DownloadSessionID m_session_id {};
	mutable std::mutex m_mutex {};
	std::unordered_map< WorkID, DownloadSessionUrlID > m_rows {};
	std::unordered_set< DownloadSessionUrlID > m_import_rows {};
	std::unordered_map< DownloadSessionUrlID, LiveUrl > m_live {};
	std::map< std::pair< DownloadSessionUrlID, std::string >, std::deque< DownloadSessionUrlID > > m_saved_rows;
	std::unordered_map< DownloadSessionUrlID, RecordID > m_saved_imports;
	std::unordered_map< DownloadSessionUrlID, DownloadSessionUrlID > m_saved_parents;
	std::optional< DownloadSessionUrlID > takeSavedRow( DownloadSessionUrlID parent, const std::string& url );
	bool hasSavedImport( DownloadSessionUrlID row ) const;

	//! Must be called with m_mutex held, so concurrent milestones publish in the order they happened.
	void moveLiveLocked( DownloadSessionUrlID row, LiveUrl& live, std::string_view state );
	void settle( DownloadSessionUrlID row );
	folly::coro::Task< void > finishRow( DownloadSessionUrlID row, events::UrlState state );
	//! Fills in a missing failure message. Returns when the URL finished.
	folly::coro::Task< std::optional< std::int64_t > > storeFinalState(
		DownloadSessionUrlID row,
		events::UrlState& state );
	folly::coro::Task< DownloadSessionUrlID > addRow(
		DownloadSessionUrlID parent,
		std::string url,
		events::UrlState state );

  public:

	explicit SessionRowObserver( DownloadSessionID session_id );

	//! Called before starting the session's workers. Replays consume matching rows once,
	//! in original ID order, including repeated imports of the same URL.
	void restore( std::vector< rows::SavedUrl > saved );

	//! Explicit retries replace their old subtree; startup recovery preserves it.
	void discardSaved( DownloadSessionUrlID root );

	[[nodiscard]] DownloadSessionUrlID rowFor( WorkID work ) const;
	//! States of unfinished URLs that differ from what is stored, such as downloading or importing.
	[[nodiscard]] std::unordered_map< DownloadSessionUrlID, std::string > liveStates() const;

	void adopt( WorkID work, DownloadSessionUrlID row );
	void forget( WorkID work );

	folly::coro::Task< void > onFinished() override;
	folly::coro::Task< void > onStarted( WorkInfo info ) override;
	folly::coro::Task< void > onCompleted( WorkInfo info ) override;
	folly::coro::Task< void > onWorkFailed( WorkInfo info, WorkFailure failure ) override;
	folly::coro::Task< void > onRequestFailed( RequestFailure failure ) override;
	folly::coro::Task< std::expected< void, std::string > > onImportQueued( WorkInfo info ) override;
	folly::coro::Task< void > onImported( ImportInfo info ) override;
	folly::coro::Task< void > onImportFailure( WorkInfo info, std::string url, WorkFailure failure ) override;
	folly::coro::Task< void > onFollowed( WorkInfo info, FollowStatus status ) override;
	void onTransfer( WorkID work, TransferEvent event, bool import ) override;
	folly::coro::Task< std::expected< void, std::string > > onUrlTypeSet( std::string url, std::string type ) override;
	folly::coro::Task< std::expected< void, std::string > > onUrlTypesSet(
		std::vector< std::pair< std::string, std::string > > types ) override;
};

[[nodiscard]] std::string failureStage( FailureStage stage );

} // namespace idhan::downloader
