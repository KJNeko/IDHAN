#include "SessionRowObserver.hpp"

#include <stdexcept>

#include "DownloadSessionEventHub.hpp"
#include "DownloadSessionManager.hpp"
#include "logging/log.hpp"
#include "sessionRows.hpp"
#include "urls/AssociateUrls.hpp"

namespace idhan::downloader
{

constexpr std::string_view missing_failure_reason { "The download failed without a reason" };

std::string failureStage( const FailureStage stage )
{
	switch ( stage )
	{
		case FailureStage::ROUTE:
			return "route";
		case FailureStage::DOWNLOAD:
			return "download";
		case FailureStage::PARSER:
			return "parser";
		case FailureStage::IMPORT:
			return "import";
		case FailureStage::CLOSED:
			return "closed";
	}
	return "parser";
}

static bool isTerminal( const std::string_view state )
{
	return state != "pending" && state != "processing";
}

SessionRowObserver::SessionRowObserver( const DownloadSessionID session_id ) : m_session_id( session_id )
{}

void SessionRowObserver::restore( std::vector< rows::SavedUrl > saved )
{
	const std::scoped_lock lock { m_mutex };
	for ( auto& row : saved )
	{
		m_saved_parents.emplace( row.id, row.parent );
		m_saved_rows[ { row.parent, std::move( row.url ) } ].push_back( row.id );
		if ( !row.record.has_value() ) continue;

		if ( row.state != "imported" && row.state != "imported_previously" ) continue;

		m_saved_imports.emplace( row.id, row.record.value() );
	}
}

void SessionRowObserver::discardSaved( const DownloadSessionUrlID root )
{
	const std::scoped_lock lock { m_mutex };
	std::unordered_set< DownloadSessionUrlID > discarded { root };
	bool changed { true };
	while ( changed )
	{
		changed = false;
		for ( const auto& [ row, parent ] : m_saved_parents )
		{
			if ( !discarded.contains( parent ) ) continue;

			if ( discarded.insert( row ).second ) changed = true;
		}
	}
	for ( const auto row : discarded )
	{
		m_saved_imports.erase( row );
		m_saved_parents.erase( row );
	}
	for ( auto entry { m_saved_rows.begin() }; entry != m_saved_rows.end(); )
	{
		std::erase_if(
			entry->second, [ &discarded ]( const DownloadSessionUrlID row ) { return discarded.contains( row ); } );
		if ( entry->second.empty() )
			entry = m_saved_rows.erase( entry );
		else
			++entry;
	}
}

std::optional< DownloadSessionUrlID > SessionRowObserver::takeSavedRow(
	const DownloadSessionUrlID parent,
	const std::string& url )
{
	const std::scoped_lock lock { m_mutex };
	const auto found { m_saved_rows.find( { parent, url } ) };
	if ( found == m_saved_rows.end() ) return std::nullopt;

	const auto row { found->second.front() };
	found->second.pop_front();
	if ( found->second.empty() ) m_saved_rows.erase( found );

	return row;
}

bool SessionRowObserver::hasSavedImport( const DownloadSessionUrlID row ) const
{
	const std::scoped_lock lock { m_mutex };
	return m_saved_imports.contains( row );
}

void SessionRowObserver::adopt( const WorkID work, const DownloadSessionUrlID row )
{
	const std::scoped_lock lock { m_mutex };
	m_rows.insert_or_assign( work, row );
	const bool imported { m_saved_imports.contains( row ) };
	if ( imported )
		m_import_rows.insert( row );
	else
		m_import_rows.erase( row );

	m_live.insert_or_assign( row, LiveUrl { .terminal = imported } );
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

std::unordered_map< DownloadSessionUrlID, std::string > SessionRowObserver::liveStates() const
{
	const std::scoped_lock lock { m_mutex };
	std::unordered_map< DownloadSessionUrlID, std::string > states;
	for ( const auto& [ row, live ] : m_live )
	{
		if ( live.terminal ) continue;

		if ( live.state == "pending" ) continue;

		states.emplace( row, live.state );
	}
	return states;
}

void SessionRowObserver::moveLiveLocked( const DownloadSessionUrlID row, LiveUrl& live, const std::string_view state )
{
	if ( live.terminal ) return;

	if ( live.state == state ) return;

	live.state = state;
	events::urlUpdated( m_session_id, row, events::UrlState { .state = std::string { state } } );
}

void SessionRowObserver::settle( const DownloadSessionUrlID row )
{
	const std::scoped_lock lock { m_mutex };
	m_live[ row ].terminal = true;
}

folly::coro::Task< void > SessionRowObserver::finishRow( const DownloadSessionUrlID row, events::UrlState state )
{
	if ( row == 0 ) co_return;

	if ( hasSavedImport( row ) ) co_return;

	settle( row );
	state.finished_at = co_await storeFinalState( row, state );
	events::urlUpdated( m_session_id, row, state );
}

folly::coro::Task< std::optional< std::int64_t > > SessionRowObserver::storeFinalState(
	const DownloadSessionUrlID row,
	events::UrlState& state )
{
	auto& db { DownloadSessionManager::dbContext() };
	if ( state.state != "failed" )
	{
		const auto finished_e { co_await rows::mark( m_session_id, row, state.state, db ) };
		if ( !finished_e.has_value() ) throw std::runtime_error( finished_e.error() );

		co_return finished_e.value();
	}

	if ( !state.error.has_value() || state.error->empty() ) state.error = std::string { missing_failure_reason };

	const auto finished_e { co_await rows::fail(
		m_session_id, row, state.stage.value_or( failureStage( FailureStage::PARSER ) ), state.error.value(), db ) };
	if ( !finished_e.has_value() ) throw std::runtime_error( finished_e.error() );

	co_return finished_e.value();
}

folly::coro::Task< DownloadSessionUrlID > SessionRowObserver::addRow(
	const DownloadSessionUrlID parent,
	std::string url,
	events::UrlState state )
{
	const auto saved { takeSavedRow( parent, url ) };
	if ( saved.has_value() ) co_return saved.value();

	// A failure is stored apart from the row, so the row starts pending and fails afterwards.
	const bool failed { state.state == "failed" };
	const auto inserted_e { co_await rows::insert(
		m_session_id, parent, url, failed ? "pending" : state.state, DownloadSessionManager::dbContext() ) };
	if ( !inserted_e.has_value() ) throw std::runtime_error( inserted_e.error() );

	const auto& inserted { inserted_e.value() };
	state.finished_at = failed ? co_await storeFinalState( inserted.id, state ) : inserted.finished_at;

	if ( isTerminal( state.state ) )
	{
		const std::scoped_lock lock { m_mutex };
		m_live[ inserted.id ].terminal = true;
	}

	events::UrlRow created {
		.url_id = inserted.id,
		.parent_id = parent == 0 ? std::nullopt : std::optional { parent },
		.url = std::move( url ),
		.created_at = inserted.created_at,
		.state = events::UrlState { .state = "pending" }
	};

	// Only a URL the session already saw is settled when it is created; every other outcome follows as an update.
	const bool settled_on_creation { state.state == "pending" || state.state == "skipped_already_seen" };
	if ( settled_on_creation ) created.state = state;

	events::urlCreated( m_session_id, created );

	if ( !settled_on_creation ) events::urlUpdated( m_session_id, inserted.id, state );

	co_return inserted.id;
}

folly::coro::Task< void > SessionRowObserver::onFinished()
{
	DownloadSessionEventHub::instance().finished( m_session_id );
	co_return;
}

folly::coro::Task< void > SessionRowObserver::onStarted( WorkInfo info )
{
	const auto row { rowFor( info.id ) };
	if ( row == 0 ) co_return;

	if ( hasSavedImport( row ) ) co_return;

	const auto marked_e { co_await rows::mark( m_session_id, row, "processing", DownloadSessionManager::dbContext() ) };
	if ( !marked_e.has_value() ) throw std::runtime_error( marked_e.error() );

	const std::scoped_lock lock { m_mutex };
	auto& live { m_live[ row ] };
	// A request may already have started; its milestones are newer than this notification.
	if ( live.state != "pending" ) co_return;

	moveLiveLocked( row, live, "processing" );
}

folly::coro::Task< void > SessionRowObserver::onCompleted( WorkInfo info )
{
	const auto row { rowFor( info.id ) };
	if ( row == 0 ) co_return;

	bool imported_row {};
	{
		const std::scoped_lock lock { m_mutex };
		imported_row = m_import_rows.contains( row );
	}
	if ( !imported_row ) co_await finishRow( row, events::UrlState { .state = "completed" } );

	const auto cleared_e {
		co_await rows::clearRetriedErrors( m_session_id, row, DownloadSessionManager::dbContext() )
	};
	if ( !cleared_e.has_value() ) throw std::runtime_error( cleared_e.error() );

	events::requestErrorsCleared( m_session_id, cleared_e.value() );
}

folly::coro::Task< void > SessionRowObserver::onWorkFailed( WorkInfo info, WorkFailure failure )
{
	log::warn( "Downloader session {}: {} failed: {}", m_session_id, info.url, failure.message );
	co_await finishRow(
		rowFor( info.id ),
		events::UrlState {
			.state = "failed", .stage = failureStage( failure.stage ), .error = std::move( failure.message ) } );
}

folly::coro::Task< void > SessionRowObserver::onRequestFailed( RequestFailure failure )
{
	const auto row { rowFor( failure.work ) };
	const auto logged_e { co_await rows::addError(
		m_session_id,
		row,
		failure.url,
		failure.lane,
		failure.status,
		failure.message,
		DownloadSessionManager::dbContext() ) };
	if ( !logged_e.has_value() ) throw std::runtime_error( logged_e.error() );

	const auto& logged { logged_e.value() };
	events::requestError(
		m_session_id,
		events::RequestError {
			.error_id = logged.id,
			.url_id = row == 0 ? std::nullopt : std::optional { row },
			.url = std::move( failure.url ),
			.lane = std::move( failure.lane ),
			.http_status = failure.status,
			.message = failure.message.empty() ? std::nullopt : std::optional { std::move( failure.message ) },
			.occurred_at = logged.occurred_at } );
}

folly::coro::Task< std::expected< void, std::string > > SessionRowObserver::onImportQueued( WorkInfo info )
{
	const auto parent { rowFor( info.parent.value_or( 0 ) ) };
	if ( parent != 0 )
	{
		const auto url_e { co_await rows::url( m_session_id, parent, DownloadSessionManager::dbContext() ) };
		if ( !url_e.has_value() ) co_return std::unexpected( url_e.error() );

		// The parser asked to import its own URL, so that URL's row records the import.
		if ( url_e.value() == info.url )
		{
			const std::scoped_lock lock { m_mutex };
			m_rows.insert_or_assign( info.id, parent );
			m_import_rows.insert( parent );
			co_return std::expected< void, std::string > {};
		}
	}

	try
	{
		const auto row { co_await addRow( parent, info.url, events::UrlState { .state = "pending" } ) };
		adopt( info.id, row );
	}
	catch ( const std::exception& error )
	{
		co_return std::unexpected( std::string { error.what() } );
	}

	co_return std::expected< void, std::string > {};
}

folly::coro::Task< void > SessionRowObserver::onImported( ImportInfo info )
{
	const auto row { rowFor( info.work ) };
	{
		const std::scoped_lock lock { m_mutex };
		const auto saved { m_saved_imports.find( row ) };
		if ( saved != m_saved_imports.end() )
		{
			if ( saved->second == info.record_id ) co_return;

			m_saved_imports.erase( saved );
		}
	}
	const std::string state { info.existing ? "imported_previously" : "imported" };
	settle( row );
	const auto completed_e { co_await rows::completeImport(
		m_session_id, row, info.record_id, state, DownloadSessionManager::dbContext(), info.posted_at ) };
	if ( !completed_e.has_value() ) throw std::runtime_error( completed_e.error() );

	const auto& completed { completed_e.value() };
	if ( !completed.has_value() ) throw std::runtime_error( "The imported URL no longer exists" );

	events::urlUpdated(
		m_session_id,
		row,
		events::UrlState {
			.state = state,
			.record_id = info.record_id,
			.size = completed->size,
			.finished_at = completed->finished_at } );

	log::info( "Downloader session {} imported {} as record {}", m_session_id, info.url, info.record_id );
}

folly::coro::Task< void > SessionRowObserver::onImportFailure( WorkInfo info,
	std::string url,
	WorkFailure failure )
{
	events::UrlState state {
		.state = "failed", .stage = failureStage( failure.stage ), .error = std::move( failure.message )
	};
	const auto row { rowFor( info.id ) };
	if ( row != 0 )
	{
		co_await finishRow( row, std::move( state ) );
		co_return;
	}

	const auto parent { info.parent.has_value() ? rowFor( info.parent.value() ) : DownloadSessionUrlID {} };
	(void)co_await addRow( parent, std::move( url ), std::move( state ) );
}

folly::coro::Task< void > SessionRowObserver::onFollowed( WorkInfo info, const FollowStatus status )
{
	const auto parent { rowFor( info.parent.value_or( 0 ) ) };

	switch ( status )
	{
		case FollowStatus::QUEUED:
			{
				const auto row { co_await addRow( parent, info.url, events::UrlState { .state = "pending" } ) };
				adopt( info.id, row );
				break;
			}
		case FollowStatus::FILTERED:
			(void)co_await addRow( parent, info.url, events::UrlState { .state = "skipped_no_url_class" } );
			break;
		case FollowStatus::ALREADY_QUEUED:
		case FollowStatus::ALREADY_EXPLORED:
			(void)co_await addRow( parent, info.url, events::UrlState { .state = "skipped_already_seen" } );
			break;
		case FollowStatus::ALREADY_IMPORTED:
			(void)co_await addRow( parent, info.url, events::UrlState { .state = "skipped_already_imported" } );
			break;
	}
}

void SessionRowObserver::onTransfer( const WorkID work, const TransferEvent event, const bool import )
{
	const std::scoped_lock lock { m_mutex };
	const auto found { m_rows.find( work ) };
	if ( found == m_rows.end() ) return;

	const auto row { found->second };
	auto& live { m_live[ row ] };

	if ( import )
	{
		if ( event == TransferEvent::DOWNLOADING ) moveLiveLocked( row, live, "downloading" );

		if ( event == TransferEvent::STORING ) moveLiveLocked( row, live, "importing" );

		return;
	}

	if ( event == TransferEvent::DOWNLOADING )
	{
		++live.downloads;
		moveLiveLocked( row, live, "downloading" );
		return;
	}

	if ( event != TransferEvent::FINISHED ) return;

	if ( live.downloads > 0 ) --live.downloads;

	if ( live.downloads > 0 ) return;

	if ( live.state == "downloading" ) moveLiveLocked( row, live, "processing" );
}

folly::coro::Task< std::expected< void, std::string > > SessionRowObserver::onUrlTypeSet(
	std::string url,
	std::string type )
{
	co_return co_await helpers::setUrlType( std::move( url ), std::move( type ), DownloadSessionManager::dbContext() );
}

folly::coro::Task< std::expected< void, std::string > > SessionRowObserver::onUrlTypesSet(
	std::vector< std::pair< std::string, std::string > > types )
{
	co_return co_await helpers::setUrlTypes( std::move( types ), DownloadSessionManager::dbContext() );
}


} // namespace idhan::downloader
