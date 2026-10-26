#include "sessionRows.hpp"

#include "idhan/db/DBContext.hpp"

namespace idhan::downloader::rows
{
DbResult< DownloadSessionUrlID > insert(
	DownloadSessionID session_id,
	DownloadSessionUrlID parent_row,
	std::string url,
	std::string state,
	std::string message,
	db::DBContext& db )
{
	try
	{
		db::DBBindings bindings {};
		bindings.bind( session_id );
		bindings.bind( url );
		bindings.bind( state );
		bindings.bind( parent_row );
		bindings.bind( message );

		const auto result {
			co_await db
				.query(
					"INSERT INTO download_session_urls "
					"(download_session_id, url, state, parent_url_id, note, error, finished_at) VALUES "
					"($1, $2, $3, NULLIF($4, 0::bigint), "
					"CASE WHEN $3 = 'failed' THEN NULL ELSE NULLIF($5, '') END, "
					"CASE WHEN $3 = 'failed' THEN COALESCE(NULLIF($5, ''), 'The download failed without a reason') END, "
					"CASE WHEN $3 IN ('pending', 'processing') THEN NULL ELSE now() END) "
					"RETURNING download_session_url_id",
					std::move( bindings ) )
				.rows< DownloadSessionUrlID >()
		};

		co_return std::get< 0 >( result.row( 0 ) );
	}
	catch ( const std::exception& error )
	{
		co_return std::unexpected( std::string { error.what() } );
	}
}

DbResult< void > mark( DownloadSessionUrlID row_id, std::string target, std::string error, db::DBContext& db )
{
	try
	{
		db::DBBindings bindings {};
		bindings.bind( row_id );
		bindings.bind( target );
		bindings.bind( error );

		co_await db.query(
			"UPDATE download_session_urls SET state = $2, "
			"finished_at = CASE WHEN $2 IN ('pending', 'processing') THEN NULL ELSE now() END, "
			"error = CASE WHEN $2 = 'failed' "
			"THEN COALESCE(NULLIF($3, ''), 'The download failed without a reason') END "
			"WHERE download_session_url_id = $1",
			std::move( bindings ) );
		co_return std::expected< void, std::string > {};
	}
	catch ( const std::exception& caught )
	{
		co_return std::unexpected( std::string { caught.what() } );
	}
}

DbResult< void > addError(
	DownloadSessionID session_id,
	DownloadSessionUrlID row_id,
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
		bindings.bind( row_id );
		bindings.bind( url );
		bindings.bind( lane );
		bindings.bind( status );
		bindings.bind( message );

		co_await db.query(
			"INSERT INTO download_session_errors "
			"(download_session_id, download_session_url_id, url, lane, status, message) VALUES "
			"($1, NULLIF($2, 0::bigint), $3, $4, $5, NULLIF($6, ''))",
			std::move( bindings ) );
		co_return std::expected< void, std::string > {};
	}
	catch ( const std::exception& error )
	{
		co_return std::unexpected( std::string { error.what() } );
	}
}

DbResult< void > completeImport( DownloadSessionUrlID row_id, RecordID record, std::string note, db::DBContext& db )
{
	try
	{
		db::DBBindings bindings {};
		bindings.bind( row_id );
		bindings.bind( record );
		bindings.bind( note );

		co_await db.query(
			"UPDATE download_session_urls SET record_id = $2, state = 'completed', "
			"finished_at = now(), error = NULL, note = NULLIF($3, '') "
			"WHERE download_session_url_id = $1",
			std::move( bindings ) );
		co_return std::expected< void, std::string > {};
	}
	catch ( const std::exception& error )
	{
		co_return std::unexpected( std::string { error.what() } );
	}
}
} // namespace idhan::downloader::rows
