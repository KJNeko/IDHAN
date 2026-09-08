#include "sessionRows.hpp"

namespace idhan::downloader::rows
{
Result< DownloadSessionUrlID > insert(
	DownloadSessionID session_id,
	DownloadSessionUrlID parent_row,
	std::string url,
	std::string state,
	std::string message,
	drogon::orm::DbClientPtr db )
{
	try
	{
		const auto inserted { co_await db->execSqlCoro(
			"INSERT INTO download_session_urls "
			"(download_session_id, url, state, parent_url_id, note, error, finished_at) VALUES "
			"($1, $2, $3, NULLIF($4, 0::bigint), "
			"CASE WHEN $3 = 'failed' THEN NULL ELSE NULLIF($5, '') END, "
			"CASE WHEN $3 = 'failed' THEN COALESCE(NULLIF($5, ''), 'The download failed without a reason') END, "
			"CASE WHEN $3 IN ('pending', 'processing') THEN NULL ELSE now() END) "
			"RETURNING download_session_url_id",
			session_id,
			url,
			std::string { state },
			parent_row,
			message ) };
		co_return inserted[ 0 ][ "download_session_url_id" ].as< DownloadSessionUrlID >();
	}
	catch ( const std::exception& error )
	{
		co_return std::unexpected( std::string { error.what() } );
	}
}

Result< void > mark( DownloadSessionUrlID row_id, std::string target, std::string error, drogon::orm::DbClientPtr db )
{
	try
	{
		co_await db->execSqlCoro(
			"UPDATE download_session_urls SET state = $2, "
			"finished_at = CASE WHEN $2 IN ('pending', 'processing') THEN NULL ELSE now() END, "
			"error = CASE WHEN $2 = 'failed' "
			"THEN COALESCE(NULLIF($3, ''), 'The download failed without a reason') END "
			"WHERE download_session_url_id = $1",
			row_id,
			target,
			error );
		co_return std::expected< void, std::string > {};
	}
	catch ( const std::exception& error )
	{
		co_return std::unexpected( std::string { error.what() } );
	}
}

Result< void > addError(
	DownloadSessionID session_id,
	DownloadSessionUrlID row_id,
	std::string url,
	std::string lane,
	std::optional< std::int32_t > status,
	std::string message,
	drogon::orm::DbClientPtr db )
{
	try
	{
		co_await db->execSqlCoro(
			"INSERT INTO download_session_errors "
			"(download_session_id, download_session_url_id, url, lane, status, message) VALUES "
			"($1, NULLIF($2, 0::bigint), $3, $4, $5, NULLIF($6, ''))",
			session_id,
			row_id,
			url,
			lane,
			status,
			message );
		co_return std::expected< void, std::string > {};
	}
	catch ( const std::exception& error )
	{
		co_return std::unexpected( std::string { error.what() } );
	}
}

Result< void > completeImport(
	DownloadSessionUrlID row_id,
	RecordID record,
	std::string note,
	drogon::orm::DbClientPtr db )
{
	try
	{
		co_await db->execSqlCoro(
			"UPDATE download_session_urls SET record_id = $2, state = 'completed', "
			"finished_at = now(), error = NULL, note = NULLIF($3, '') "
			"WHERE download_session_url_id = $1",
			row_id,
			record,
			note );
		co_return std::expected< void, std::string > {};
	}
	catch ( const std::exception& error )
	{
		co_return std::unexpected( std::string { error.what() } );
	}
}
} // namespace idhan::downloader::rows
