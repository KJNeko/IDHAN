#include "sessionTree.hpp"

#include <algorithm>
#include <unordered_map>
#include <vector>

namespace idhan::downloader
{

using UrlNodes = std::unordered_map< DownloadSessionUrlID, Json::Value >;
using UrlChildren = std::unordered_map< DownloadSessionUrlID, std::vector< DownloadSessionUrlID > >;

struct ImportedRecord
{
	RecordID record_id {};
	std::int64_t imported_at {};
	DownloadSessionUrlID url_id {};
};

using UrlRecordImports = std::unordered_map< DownloadSessionUrlID, ImportedRecord >;

static Json::Value urlNodeJson( const drogon::orm::Row& row )
{
	Json::Value json {};

	json[ "id" ] = row[ "download_session_url_id" ].as< DownloadSessionUrlID >();
	json[ "parent_id" ] = row[ "parent_url_id" ].isNull() ?
	                          Json::Value {} :
	                          Json::Value { row[ "parent_url_id" ].as< DownloadSessionUrlID >() };
	json[ "url" ] = row[ "url" ].as< std::string >();
	json[ "state" ] = row[ "state" ].as< std::string >();
	json[ "created_at" ] = row[ "created_at" ].as< std::int64_t >();
	json[ "finished_at" ] =
		row[ "finished_at" ].isNull() ? Json::Value {} : Json::Value { row[ "finished_at" ].as< std::int64_t >() };
	json[ "error" ] = row[ "error" ].isNull() ? Json::Value {} : Json::Value { row[ "error" ].as< std::string >() };
	json[ "note" ] = row[ "note" ].isNull() ? Json::Value {} : Json::Value { row[ "note" ].as< std::string >() };
	json[ "record_id" ] =
		row[ "record_id" ].isNull() ? Json::Value {} : Json::Value { row[ "record_id" ].as< RecordID >() };

	return json;
}

static Json::Value urlTreeJson( const DownloadSessionUrlID id, const UrlNodes& nodes, const UrlChildren& children )
{
	Json::Value json { nodes.at( id ) };
	Json::Value list { Json::arrayValue };
	const auto found { children.find( id ) };

	if ( found != children.end() )
		for ( const auto child : found->second ) list.append( urlTreeJson( child, nodes, children ) );

	json[ "children" ] = list;
	return json;
}

static void collectUrlSubtree(
	const DownloadSessionUrlID id,
	const UrlNodes& nodes,
	const UrlChildren& children,
	const UrlRecordImports& imports,
	Json::Value& urls,
	std::unordered_map< RecordID, ImportedRecord >& records )
{
	const auto imported { imports.find( id ) };
	if ( imported != imports.end() ) records.try_emplace( imported->second.record_id, imported->second );

	const auto found { children.find( id ) };
	if ( found == children.end() ) return;

	for ( const auto child : found->second )
	{
		urls.append( nodes.at( child ) );
		collectUrlSubtree( child, nodes, children, imports, urls, records );
	}
}

drogon::Task< Json::Value > sessionUrlTree(
	drogon::orm::DbClientPtr db,
	const DownloadSessionID session_id,
	const bool flatten )
{
	const auto rows { co_await db->execSqlCoro(
		"SELECT download_session_url_id, parent_url_id, url, state, error, note, download_session_urls.record_id, "
		"(extract(epoch FROM download_session_records.imported_at) * 1000000)::bigint AS imported_at, "
		"extract(epoch FROM created_at)::bigint AS created_at, "
		"extract(epoch FROM finished_at)::bigint AS finished_at "
		"FROM download_session_urls "
		"LEFT JOIN download_session_records ON download_session_records.download_session_id = $1 "
		"AND download_session_records.record_id = download_session_urls.record_id "
		"WHERE download_session_urls.download_session_id = $1 "
		"ORDER BY download_session_urls.download_session_url_id",
		session_id ) };

	UrlNodes nodes {};
	UrlChildren children {};
	UrlRecordImports imports {};
	std::vector< DownloadSessionUrlID > roots {};

	for ( const auto& row : rows )
	{
		const auto row_id { row[ "download_session_url_id" ].as< DownloadSessionUrlID >() };
		nodes.emplace( row_id, urlNodeJson( row ) );
		if ( !row[ "record_id" ].isNull() && !row[ "imported_at" ].isNull() )
			imports.emplace(
				row_id,
				ImportedRecord {
					.record_id = row[ "record_id" ].as< RecordID >(),
					.imported_at = row[ "imported_at" ].as< std::int64_t >(),
					.url_id = row_id,
				} );

		if ( row[ "parent_url_id" ].isNull() )
			roots.emplace_back( row_id );
		else
			children[ row[ "parent_url_id" ].as< DownloadSessionUrlID >() ].emplace_back( row_id );
	}

	std::ranges::reverse( roots );
	Json::Value output { Json::arrayValue };

	for ( const auto root : roots )
	{
		if ( !flatten )
		{
			output.append( urlTreeJson( root, nodes, children ) );
			continue;
		}

		Json::Value entry { nodes.at( root ) };
		Json::Value discovered { Json::arrayValue };
		Json::Value record_ids { Json::arrayValue };
		std::unordered_map< RecordID, ImportedRecord > records {};
		collectUrlSubtree( root, nodes, children, imports, discovered, records );
		std::vector< ImportedRecord > ordered_records {};
		ordered_records.reserve( records.size() );

		for ( const auto& [ _, imported ] : records )
		{
			ordered_records.emplace_back( imported );
		}

		std::ranges::sort(
			ordered_records,
			[]( const ImportedRecord& left, const ImportedRecord& right )
			{
				if ( left.imported_at != right.imported_at ) return left.imported_at < right.imported_at;
				if ( left.url_id != right.url_id ) return left.url_id < right.url_id;
				return left.record_id < right.record_id;
			} );
		for ( const auto& imported : ordered_records ) record_ids.append( imported.record_id );
		entry[ "urls" ] = discovered;
		entry[ "record_ids" ] = record_ids;
		output.append( entry );
	}

	co_return output;
}

drogon::Task< Json::Value > sessionSummary( drogon::orm::DbClientPtr db, const DownloadSessionID session_id )
{
	const auto rows { co_await db->execSqlCoro(
		"SELECT download_session_id, name, "
		"extract(epoch FROM created_at)::bigint AS created_at, "
		"extract(epoch FROM last_used_at)::bigint AS last_used_at, "
		"(SELECT count(*) FROM download_session_errors "
		"WHERE download_session_errors.download_session_id = download_sessions.download_session_id) AS error_count "
		"FROM download_sessions WHERE download_session_id = $1",
		session_id ) };

	if ( rows.empty() ) co_return Json::Value {};

	Json::Value json {};
	json[ "id" ] = rows[ 0 ][ "download_session_id" ].as< DownloadSessionID >();
	json[ "name" ] = rows[ 0 ][ "name" ].as< std::string >();
	json[ "created_at" ] = rows[ 0 ][ "created_at" ].as< std::int64_t >();
	json[ "last_used_at" ] = rows[ 0 ][ "last_used_at" ].as< std::int64_t >();
	json[ "error_count" ] = rows[ 0 ][ "error_count" ].as< std::int64_t >();
	co_return json;
}

} // namespace idhan::downloader
