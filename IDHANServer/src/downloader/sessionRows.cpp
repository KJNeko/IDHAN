#include "sessionRows.hpp"

#include <algorithm>
#include <chrono>
#include <format>

#include "SessionUrlIds.hpp"
#include "idhan/db/DBContext.hpp"

namespace idhan::downloader::rows
{
DbResult< std::vector< SavedUrl > > savedUrls( const DownloadSessionID session_id, db::DBContext& db )
{
	try
	{
		db::DBBindings bindings;
		bindings.bind( session_id );
		const auto rows {
			co_await db
				.query(
					"SELECT download_session_url_id, COALESCE(parent_url_id, 0), url, state, record_id "
					"FROM download_session_urls WHERE download_session_id = $1 ORDER BY download_session_url_id",
					std::move( bindings ) )
				.rows<
					DownloadSessionUrlID,
					DownloadSessionUrlID,
					std::string,
					std::string,
					std::optional< RecordID > >()
		};
		std::vector< SavedUrl > saved;
		saved.reserve( rows.size() );
		for ( const auto& [ id, parent, url, state, record ] : rows )
			saved.push_back( SavedUrl { id, parent, url, state, record } );

		co_return saved;
	}
	catch ( const std::exception& error )
	{
		co_return std::unexpected( std::string { error.what() } );
	}
}

DbResult< std::optional< std::string > > url(
	const DownloadSessionID session_id,
	const DownloadSessionUrlID id,
	db::DBContext& db )
{
	try
	{
		db::DBBindings bindings;
		bindings.bind( session_id );
		bindings.bind( id );
		const auto rows {
			co_await db
				.query(
					"SELECT url FROM download_session_urls "
					"WHERE download_session_id = $1 AND download_session_url_id = $2",
					std::move( bindings ) )
				.rows< std::string >()
		};
		if ( rows.empty() ) co_return std::optional< std::string > {};

		co_return std::optional< std::string > { std::get< 0 >( rows.row( 0 ) ) };
	}
	catch ( const std::exception& error )
	{
		co_return std::unexpected( std::string { error.what() } );
	}
}

DbResult< InsertedUrl > insert(
	const DownloadSessionID session_id,
	const DownloadSessionUrlID parent,
	std::string url,
	std::string state,
	db::DBContext& db )
{
	try
	{
		db::DBBindings bindings {};
		bindings.bind( session_id );
		bindings.bind( parent );
		bindings.bind( url );
		bindings.bind( state );
		bindings.bind( SessionUrlIds::instance().reserve( session_id ) );

		const auto rows {
			co_await db
				.query(
					"INSERT INTO download_session_urls "
					"(download_session_id, download_session_url_id, parent_url_id, url, state, finished_at) VALUES "
					"($1, $5, NULLIF($2, 0), $3, $4::text, "
					"CASE WHEN $4::text IN ('pending', 'processing') THEN NULL ELSE now() END) "
					"RETURNING download_session_url_id, extract(epoch FROM created_at)::bigint, "
					"extract(epoch FROM finished_at)::bigint",
					std::move( bindings ) )
				.rows< DownloadSessionUrlID, std::int64_t, std::optional< std::int64_t > >()
		};
		if ( rows.empty() ) co_return std::unexpected( std::format( "No download session with id {}", session_id ) );

		const auto& [ id, created_at, finished_at ] { rows.row( 0 ) };
		co_return InsertedUrl { .id = id, .created_at = created_at, .finished_at = finished_at };
	}
	catch ( const std::exception& caught )
	{
		co_return std::unexpected( std::string { caught.what() } );
	}
}

DbResult< std::optional< CompletedImport > > completeImport(
	const DownloadSessionID session_id,
	const DownloadSessionUrlID id,
	const RecordID record,
	std::string state,
	db::DBContext& db,
	const std::optional< std::int64_t > posted_at )
{
	try
	{
		db::DBBindings bindings;
		bindings.bind( session_id );
		bindings.bind( id );
		bindings.bind( record );
		bindings.bind( state );
		bindings.bind( posted_at );
		const auto rows {
			co_await db
				.query(
					"UPDATE download_session_urls "
					"SET record_id = $3::integer, state = $4::text, finished_at = now(), posted_at=to_timestamp($5::double precision), post_dates_complete=($5 IS NOT NULL) "
					"WHERE download_session_id = $1 AND download_session_url_id = $2 "
					"RETURNING extract(epoch FROM finished_at)::bigint, "
					"(SELECT size FROM file_info WHERE file_info.record_id = $3::integer)",
					std::move( bindings ) )
				.rows< std::int64_t, std::optional< std::int64_t > >()
		};
		if ( rows.empty() ) co_return std::optional< CompletedImport > {};

		const auto& [ finished_at, size ] { rows.row( 0 ) };
		co_return std::optional< CompletedImport > { CompletedImport {
			.id = id, .finished_at = finished_at, .size = size } };
	}
	catch ( const std::exception& error )
	{
		co_return std::unexpected( std::string { error.what() } );
	}
}

DbResult< std::optional< std::int64_t > > mark(
	const DownloadSessionID session_id,
	const DownloadSessionUrlID id,
	std::string state,
	db::DBContext& db )
{
	try
	{
		db::DBBindings bindings {};
		bindings.bind( session_id );
		bindings.bind( id );
		bindings.bind( state );

		const auto rows {
			co_await db
				.query(
					"UPDATE download_session_urls SET state = $3::text, "
					"finished_at = CASE WHEN $3::text IN ('pending', 'processing') THEN NULL ELSE now() END "
					"WHERE download_session_id = $1 AND download_session_url_id = $2 "
					"RETURNING extract(epoch FROM finished_at)::bigint",
					std::move( bindings ) )
				.rows< std::optional< std::int64_t > >()
		};
		if ( rows.empty() ) co_return std::optional< std::int64_t > {};

		co_return std::get< 0 >( rows.row( 0 ) );
	}
	catch ( const std::exception& caught )
	{
		co_return std::unexpected( std::string { caught.what() } );
	}
}

DbResult< std::int64_t > fail(
	const DownloadSessionID session_id,
	const DownloadSessionUrlID id,
	std::string stage,
	std::string message,
	db::DBContext& db )
{
	try
	{
		db::DBBindings bindings {};
		bindings.bind( session_id );
		bindings.bind( id );
		bindings.bind( stage );
		bindings.bind( message );

		const auto rows {
			co_await db
				.query(
					"WITH failed AS (UPDATE download_session_urls SET state = 'failed', finished_at = now() "
					"WHERE download_session_id = $1 AND download_session_url_id = $2 "
					"RETURNING download_session_id, download_session_url_id, finished_at), "
					"recorded AS (INSERT INTO download_session_url_failures "
					"(download_session_id, download_session_url_id, stage, message) "
					"SELECT download_session_id, download_session_url_id, $3, "
					"COALESCE(NULLIF($4, ''), 'The download failed without a reason') FROM failed "
					"ON CONFLICT (download_session_id, download_session_url_id) "
					"DO UPDATE SET stage = EXCLUDED.stage, message = EXCLUDED.message) "
					"SELECT extract(epoch FROM finished_at)::bigint FROM failed",
					std::move( bindings ) )
				.rows< std::int64_t >()
		};
		if ( rows.empty() )
			co_return std::unexpected( std::format( "No URL {} in download session {}", id, session_id ) );

		co_return std::get< 0 >( rows.row( 0 ) );
	}
	catch ( const std::exception& caught )
	{
		co_return std::unexpected( std::string { caught.what() } );
	}
}

DbResult< void > flagErrorsForRetry(
	const DownloadSessionID session_id,
	const DownloadSessionUrlID id,
	db::DBContext& db )
{
	try
	{
		db::DBBindings bindings {};
		bindings.bind( session_id );
		bindings.bind( id );

		co_await db.query(
			"WITH RECURSIVE retried AS ("
			"SELECT $2::integer AS url_id "
			"UNION ALL "
			"SELECT urls.download_session_url_id FROM download_session_urls urls "
			"JOIN retried ON urls.download_session_id = $1 AND urls.parent_url_id = retried.url_id"
			") "
			"UPDATE download_session_errors SET download_session_url_id = $2, retry_pending = true "
			"WHERE download_session_id = $1 "
			"AND download_session_url_id IN (SELECT url_id FROM retried)",
			std::move( bindings ) );
		co_return std::expected< void, std::string > {};
	}
	catch ( const std::exception& error )
	{
		co_return std::unexpected( std::string { error.what() } );
	}
}

DbResult< std::vector< std::int64_t > > clearRetriedErrors(
	const DownloadSessionID session_id,
	const DownloadSessionUrlID id,
	db::DBContext& db )
{
	try
	{
		db::DBBindings bindings {};
		bindings.bind( session_id );
		bindings.bind( id );

		const auto removed {
			co_await db
				.query(
					"DELETE FROM download_session_errors "
					"WHERE download_session_id = $1 AND download_session_url_id = $2 AND retry_pending "
					"RETURNING download_session_error_id",
					std::move( bindings ) )
				.rows< std::int64_t >()
		};
		std::vector< std::int64_t > ids;
		ids.reserve( removed.size() );
		for ( const auto& [ error_id ] : removed ) ids.push_back( error_id );

		co_return ids;
	}
	catch ( const std::exception& error )
	{
		co_return std::unexpected( std::string { error.what() } );
	}
}

DbResult< std::vector< DownloadSessionUrlID > > removeChildren(
	const DownloadSessionID session_id,
	const DownloadSessionUrlID id,
	db::DBContext& db )
{
	try
	{
		db::DBBindings bindings {};
		bindings.bind( session_id );
		bindings.bind( id );

		const auto removed {
			co_await db
				.query(
					"DELETE FROM download_session_urls WHERE download_session_id = $1 AND parent_url_id = $2 "
					"RETURNING download_session_url_id",
					std::move( bindings ) )
				.rows< DownloadSessionUrlID >()
		};
		std::vector< DownloadSessionUrlID > ids;
		ids.reserve( removed.size() );
		for ( const auto& [ child ] : removed ) ids.push_back( child );
		std::ranges::sort( ids );

		co_return ids;
	}
	catch ( const std::exception& error )
	{
		co_return std::unexpected( std::string { error.what() } );
	}
}

DbResult< LoggedError > addError(
	const DownloadSessionID session_id,
	const DownloadSessionUrlID id,
	std::string url,
	std::string lane,
	std::optional< std::int32_t > status,
	std::string message,
	db::DBContext& db )
{
	try
	{
		db::DBBindings bindings {};
		bindings.bind( session_id );
		bindings.bind( id );
		bindings.bind( url );
		bindings.bind( lane );
		bindings.bind( status );
		bindings.bind( message );

		const auto rows {
			co_await db
				.query(
					"INSERT INTO download_session_errors "
					"(download_session_id, download_session_url_id, url, lane, status, message) VALUES "
					"($1, NULLIF($2, 0), $3, $4, $5, NULLIF($6, '')) "
					"RETURNING download_session_error_id, extract(epoch FROM occurred_at)::bigint",
					std::move( bindings ) )
				.rows< std::int64_t, std::int64_t >()
		};
		const auto& [ error_id, occurred_at ] { rows.row( 0 ) };
		co_return LoggedError { .id = error_id, .occurred_at = occurred_at };
	}
	catch ( const std::exception& error )
	{
		co_return std::unexpected( std::string { error.what() } );
	}
}
} // namespace idhan::downloader::rows
