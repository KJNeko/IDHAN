#include "SessionResult.hpp"

#include <folly/coro/Mutex.h>

#include <stdexcept>

#include "DownloadSessionManager.hpp"
#include "SessionUrlIds.hpp"
#include "downloaderEvents.hpp"
#include "idhan/db/DBContext.hpp"
#include "idhan/db/DBTransaction.hpp"

namespace idhan::downloader
{
static folly::coro::Mutex session_retry_mutex;

runtime::Task< std::optional< SessionResult > > reportSessionResult(
	db::DBContext& db,
	const DownloadSessionID session_id )
{
	auto transaction { co_await db.transaction() };
	db::DBBindings lock;
	lock.bind( session_id );
	co_await transaction
		.query(
			"SELECT download_session_id FROM download_sessions WHERE download_session_id = $1 FOR UPDATE",
			std::move( lock ) )
		.result();

	db::DBBindings bindings;
	bindings.bind( session_id );
	const auto rows {
		co_await transaction
			.query(
				//language=postgresql
				R"SQL(
			SELECT count(*), COALESCE(bool_or(u.state IN ('pending', 'processing')), false),
			       COALESCE(bool_or(u.state IN ('failed', 'cancelled')), false),
			       min(f.message) FILTER (WHERE u.state IN ('failed', 'cancelled')),
			       count(DISTINCT u.record_id) FILTER (WHERE u.state = 'imported'),
			       count(DISTINCT u.record_id)
			FROM download_session_urls u
			LEFT JOIN download_session_url_failures f USING (download_session_id, download_session_url_id)
			WHERE u.download_session_id = $1
		)SQL",
				std::move( bindings ) )
			.rows< std::int64_t, bool, bool, std::optional< std::string >, std::int64_t, std::int64_t >()
	};
	const auto& [ count, unfinished, failed, message, imported, total ] { rows.row( 0 ) };
	if ( unfinished ) co_return std::nullopt;

	SessionResult result { .imported_count = imported, .known_count = total - imported };
	if ( count == 0 ) result.error = "The downloader session has no work";

	if ( failed ) result.error = message.value_or( "Download session did not complete successfully" );

	db::DBBindings save;
	save.bind( session_id );
	save.bind( result.imported_count );
	save.bind( result.known_count );
	save.bind( result.error );
	const auto saved {
		co_await transaction
			.query(
				//language=postgresql
				R"SQL(
			INSERT INTO download_session_results (download_session_id, imported_count, known_count, error)
			VALUES ($1, $2, $3, $4)
			ON CONFLICT (download_session_id) DO UPDATE SET imported_count = $2,
			    known_count = $3, error = $4, finished_at = now()
			RETURNING extract(epoch FROM finished_at)::bigint
		)SQL",
				std::move( save ) )
			.rows< std::int64_t >()
	};
	result.finished_at = std::get< 0 >( saved.row( 0 ) );
	db::DBBindings counts;
	counts.bind( session_id );
	counts.bind( imported );
	const auto totals {
		co_await transaction
			.query(
				"UPDATE download_sessions SET last_scan_imported_count = $2 WHERE download_session_id = $1 RETURNING total_imported_count",
				std::move( counts ) )
			.rows< std::int64_t >()
	};
	result.total_imported_count = std::get< 0 >( totals.row( 0 ) );
	co_await transaction.commit();

	events::sessionResult( session_id, result );
	co_return result;
}

runtime::Task< std::optional< SessionResult > > sessionResult( db::DBContext& db, const DownloadSessionID session_id )
{
	db::DBBindings bindings;
	bindings.bind( session_id );
	const auto rows {
		co_await db
			.query(
				//language=postgresql
				R"SQL(
			SELECT imported_count, known_count, error, extract(epoch FROM finished_at)::bigint, s.total_imported_count
			FROM download_session_results r JOIN download_sessions s USING (download_session_id) WHERE download_session_id = $1
		)SQL",
				std::move( bindings ) )
			.rows< std::int64_t, std::int64_t, std::optional< std::string >, std::int64_t, std::int64_t >()
	};
	if ( rows.empty() ) co_return std::nullopt;

	const auto& [ imported, known, error, finished, total_imported ] { rows.row( 0 ) };
	co_return SessionResult { imported, known, error, finished, total_imported };
}

runtime::Task<> clearSessionResult( db::DBContext& db, const DownloadSessionID session_id )
{
	auto transaction { co_await db.transaction() };
	db::DBBindings lock;
	lock.bind( session_id );
	co_await transaction
		.query(
			"SELECT download_session_id FROM download_sessions WHERE download_session_id = $1 FOR UPDATE",
			std::move( lock ) )
		.result();
	db::DBBindings bindings;
	bindings.bind( session_id );
	co_await transaction
		.query( "DELETE FROM download_session_results WHERE download_session_id = $1", std::move( bindings ) )
		.result();
	co_await transaction.commit();
}

runtime::Task< std::expected< void, std::string > > retrySession(
	db::DBContext& db,
	const DownloadSessionID session_id )
{
	const auto retry_lock { co_await session_retry_mutex.co_scoped_lock() };
	auto& downloads { downloadSessionManager() };
	if ( !downloads.idle( session_id ) ) co_return std::unexpected( "The download session is already running" );

	co_await downloads.destroy( session_id );
	auto transaction { co_await db.transaction() };
	db::DBBindings lookup;
	lookup.bind( session_id );
	const auto sessions {
		co_await transaction
			.query(
				//language=postgresql
				R"SQL(
			SELECT options::text, is_subscription FROM download_sessions
			WHERE download_session_id = $1 FOR UPDATE
		)SQL",
				std::move( lookup ) )
			.rows< std::string, bool >()
	};
	if ( sessions.empty() ) co_return std::unexpected( "The download session no longer exists" );

	const auto& [ settings, subscription ] { sessions.row( 0 ) };
	db::DBBindings root_bindings;
	root_bindings.bind( session_id );
	const auto roots {
		co_await transaction
			.query(
				//language=postgresql
				R"SQL(
			SELECT download_session_url_id, url FROM download_session_urls
			WHERE download_session_id = $1 AND parent_url_id IS NULL
			ORDER BY download_session_url_id DESC
		)SQL",
				std::move( root_bindings ) )
			.rows< DownloadSessionUrlID, std::string >()
	};
	if ( roots.empty() )
	{
		co_await transaction.rollback();
		(void)co_await reportSessionResult( db, session_id );
		co_return std::unexpected( "The download session has no saved request" );
	}

	// Legacy subscription sessions can contain several historical roots. Only the latest is the saved check.
	std::vector< DownloadSessionUrlID > root_ids;
	for ( const auto& [ id, url ] : roots )
	{
		root_ids.push_back( id );
		if ( subscription ) break;
	}

	db::DBBindings remove;
	remove.bind( session_id );
	remove.bind( root_ids );
	const auto removed {
		co_await transaction
			.query(
				//language=postgresql
				R"SQL(
			DELETE FROM download_session_urls
			WHERE download_session_id = $1 AND download_session_url_id <> ALL($2::integer[])
			RETURNING download_session_url_id
		)SQL",
				std::move( remove ) )
			.rows< DownloadSessionUrlID >()
	};
	db::DBBindings reset;
	reset.bind( session_id );
	co_await transaction
		.query(
			//language=postgresql
			R"SQL(
			UPDATE download_session_urls SET state = 'pending', finished_at = NULL, record_id = NULL
			WHERE download_session_id = $1
		)SQL",
			std::move( reset ) )
		.result();
	db::DBBindings failures;
	failures.bind( session_id );
	co_await transaction
		.query( "DELETE FROM download_session_url_failures WHERE download_session_id = $1", std::move( failures ) )
		.result();
	db::DBBindings result;
	result.bind( session_id );
	co_await transaction
		.query( "DELETE FROM download_session_results WHERE download_session_id = $1", std::move( result ) )
		.result();
	co_await transaction.commit();
	for ( const auto& [ id ] : removed ) events::urlRemoved( session_id, id );
	SessionUrlIds::instance().create( session_id );
	SessionUrlIds::instance().ensureNext( session_id, root_ids.front() + 1 );

	std::optional< std::string > error;
	for ( std::size_t index {}; index < root_ids.size(); ++index )
	{
		const auto& [ id, url ] { roots.row( index ) };
		events::urlUpdated( session_id, id, { .state = "pending" } );
		std::optional< std::string > failure;
		try
		{
			const auto submitted_e { co_await downloads.submit( id, session_id, url, settings ) };
			if ( !submitted_e.has_value() ) failure = submitted_e.error();
		}
		catch ( const std::exception& caught )
		{
			failure = caught.what();
		}
		if ( !failure.has_value() ) continue;

		error = failure;
		co_await downloads.markUnresumable( { id, session_id, url, settings }, failure.value(), db );
	}
	if ( error.has_value() ) co_return std::unexpected( error.value() );

	co_return std::expected< void, std::string > {};
}
} // namespace idhan::downloader
