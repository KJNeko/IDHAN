#include "SessionRowObserver.hpp"

#include <folly/coro/BlockingWait.h>

#include <stdexcept>

#include "DownloadSessionEventHub.hpp"
#include "DebugEvents.hpp"
#include "DownloadSessionManager.hpp"
#include "logging/format_ns.hpp"
#include "logging/log.hpp"
#include "sessionRows.hpp"

namespace idhan::downloader
{

SessionRowObserver::SessionRowObserver( DownloadSessionManager& manager, const DownloadSessionID session_id ) :
  m_manager( manager ),
  m_session_id( session_id )
{}

void SessionRowObserver::adopt( const WorkID work, const DownloadSessionUrlID row )
{
	const std::scoped_lock lock { m_mutex };
	m_rows.insert_or_assign( work, row );
}

void SessionRowObserver::forget( const WorkID work )
{
	const std::scoped_lock lock { m_mutex };
	m_rows.erase( work );
}

DownloadSessionUrlID SessionRowObserver::rowFor( const WorkID work ) const
{
	const std::scoped_lock lock { m_mutex };
	const auto found { m_rows.find( work ) };
	return found == m_rows.end() ? DownloadSessionUrlID {} : found->second;
}

std::unordered_map< WorkID, DownloadSessionUrlID > SessionRowObserver::rows() const
{
	const std::scoped_lock lock { m_mutex };
	return m_rows;
}

void SessionRowObserver::onDiagnosticsChanged()
{
	notifyDebugChanged();
}

void SessionRowObserver::onRequest( [[maybe_unused]] const RequestInfo& info )
{
	notifyDebugChanged();
}

void SessionRowObserver::onFinished()
{
	notifyDebugChanged();
}

void SessionRowObserver::onStarted( const WorkInfo& info )
{
	if ( const auto row { rowFor( info.id ) }; row != 0 )
		DownloadSessionManager::markRow( m_session_id, row, "processing", {} );
}

void SessionRowObserver::onCompleted( const WorkInfo& info )
{
	if ( const auto row { rowFor( info.id ) }; row != 0 )
		DownloadSessionManager::markRow( m_session_id, row, "completed", {} );
}

void SessionRowObserver::onFailed( const WorkInfo& info, const std::string& error )
{
	log::warn( "Downloader session {}: {} failed: {}", m_session_id, info.url, error );

	if ( const auto row { rowFor( info.id ) }; row != 0 )
		DownloadSessionManager::markRow( m_session_id, row, "failed", error );
}

void SessionRowObserver::onRequestFailed( const RequestFailure& failure )
{
	DownloadSessionManager::addError(
		m_session_id, rowFor( failure.work ), failure.url, failure.lane, failure.status, failure.message );
}

std::expected< void, std::string > SessionRowObserver::onImportQueued( const WorkInfo& info )
{
	try
	{
		const auto parent { rowFor( *info.parent ) };
		const auto row { DownloadSessionManager::addRow( m_session_id, parent, info.url, "pending", {} ) };
		adopt( info.id, row );
		return {};
	}
	catch ( const std::exception& error )
	{
		return std::unexpected( format_ns::format( "Unable to persist import: {}", error.what() ) );
	}
}

void SessionRowObserver::onImported( const ImportInfo& info )
{
	const auto row { rowFor( info.work ) };
	const RecordID record { info.record_id };

	const auto result {
		folly::coro::blockingWait( rows::completeImport( row, record, info.note, DownloadSessionManager::dbContext() ) )
	};
	if ( !result ) throw std::runtime_error( result.error() );

	DownloadSessionEventHub::instance().notify( m_session_id );
	log::info( "Downloader session {} imported {} as record {}", m_session_id, info.url, record );
}

void SessionRowObserver::onImportFailed( const WorkInfo& info, const std::string& url, const std::string& error )
{
	const auto row { rowFor( info.id ) };
	if ( row != 0 )
	{
		DownloadSessionManager::markRow( m_session_id, row, "failed", error );
		return;
	}

	const auto parent { info.parent.has_value() ? rowFor( *info.parent ) : DownloadSessionUrlID {} };
	(void)DownloadSessionManager::addRow( m_session_id, parent, url, "failed", error );
}

void SessionRowObserver::onFollowed( const WorkInfo& info, const FollowStatus status )
{
	const auto parent { rowFor( *info.parent ) };

	switch ( status )
	{
		case FollowStatus::QUEUED:
			{
				const auto row { DownloadSessionManager::addRow( m_session_id, parent, info.url, "pending", {} ) };
				adopt( info.id, row );
				break;
			}
		case FollowStatus::FILTERED:
			(void)DownloadSessionManager::addRow(
				m_session_id, parent, info.url, "skipped", "No URL class accepts this URL" );
			break;
		case FollowStatus::ALREADY_QUEUED:
		case FollowStatus::ALREADY_EXPLORED:
			(void)DownloadSessionManager::addRow(
				m_session_id, parent, info.url, "skipped", "Already seen in this session" );
			break;
		case FollowStatus::ALREADY_IMPORTED:
			(void)
				DownloadSessionManager::addRow( m_session_id, parent, info.url, "skipped", "Already in the database" );
			break;
	}
}

std::optional< std::int64_t > SessionRowObserver::alreadyImported( const std::string& )
{
	return std::nullopt;
}

} // namespace idhan::downloader
