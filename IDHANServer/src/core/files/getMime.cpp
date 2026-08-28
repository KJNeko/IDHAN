#include <expected>

#include "api/helpers/createBadRequest.hpp"
#include "drogon/HttpResponse.h"
#include "mime.hpp"

namespace idhan::mime
{

drogon::Task< RecordMimeIDLookup > lookupRecordMimeID( const RecordID id, DbClientPtr db )
{
	const auto result { co_await db->execSqlCoro( "SELECT mime_id FROM file_info WHERE record_id = $1", id ) };

	if ( result.empty() ) co_return RecordMimeIDLookup { .has_file_info = false, .mime_id = std::nullopt };

	if ( result[ 0 ][ 0 ].isNull() ) co_return RecordMimeIDLookup { .has_file_info = true, .mime_id = std::nullopt };

	co_return RecordMimeIDLookup { .has_file_info = true, .mime_id = result[ 0 ][ 0 ].as< MimeID >() };
}

drogon::Task< std::expected< MimeID, drogon::HttpResponsePtr > > getMimeIDFromRecord(
	const RecordID id,
	DbClientPtr db )
{
	const auto lookup { co_await lookupRecordMimeID( id, db ) };

	if ( !lookup.mime_id ) co_return std::unexpected( createNotFound( "No file info for record {}", id ) );

	co_return *lookup.mime_id;
}

drogon::Task< std::optional< FileMimeInfo > > findMime( const MimeID mime_id, DbClientPtr db )
{
	const auto mime_search {
		co_await db->execSqlCoro( "SELECT name, best_extension FROM mime WHERE mime_id = $1", mime_id )
	};

	if ( mime_search.empty() ) co_return std::nullopt;

	FileMimeInfo info {};
	info.m_id = mime_id;
	info.extension = mime_search[ 0 ][ 1 ].as< std::string >();

	co_return info;
}

drogon::Task< std::expected< FileMimeInfo, drogon::HttpResponsePtr > > getMime( const MimeID mime_id, DbClientPtr db )
{
	const auto mime_info { co_await findMime( mime_id, db ) };

	if ( !mime_info ) co_return std::unexpected( createInternalError( "mime_id {} not found in mime table", mime_id ) );

	co_return *mime_info;
}

drogon::Task< std::expected< FileMimeInfo, drogon::HttpResponsePtr > > getRecordMime(
	const RecordID record_id,
	DbClientPtr db )
{
	const auto id { co_await getMimeIDFromRecord( record_id, db ) };

	if ( id ) co_return co_await getMime( id.value(), db );

	co_return std::unexpected( id.error() );
}

} // namespace idhan::mime
