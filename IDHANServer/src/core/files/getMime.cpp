//
// Created by kj16609 on 11/19/24.
//

#include <expected>

#include "api/helpers/createBadRequest.hpp"
#include "drogon/HttpResponse.h"
#include "mime.hpp"

namespace idhan::mime
{

drogon::Task< std::expected< MimeID, drogon::HttpResponsePtr > >
	getMimeIDFromRecord( const RecordID id, drogon::orm::DbClientPtr db )
{
	const auto result { db->execSqlSync( "SELECT mime_id FROM file_info WHERE record_id = $1", id ) };

	if ( result.empty() ) co_return std::unexpected( createBadRequest( "Invalid file info" ) );

	co_return result[ 0 ][ 0 ].as< MimeID >();
}

drogon::Task< std::expected< FileMimeInfo, drogon::HttpResponsePtr > >
	getMime( const MimeID mime_id, drogon::orm::DbClientPtr db )
{
	const auto mime_search { db->execSqlSync( "SELECT name, best_extension FROM mime WHERE mime_id = $1", mime_id ) };

	if ( mime_search.empty() ) co_return std::unexpected( createBadRequest( "Invalid mime id" ) );

	FileMimeInfo info {};
	info.m_id = mime_id;
	info.extension = mime_search[ 0 ][ 1 ].as< std::string >();

	co_return info;
}

drogon::Task< std::expected< FileMimeInfo, drogon::HttpResponsePtr > >
	getRecordMime( const RecordID record_id, drogon::orm::DbClientPtr db )
{
	const auto id { co_await getMimeIDFromRecord( record_id, db ) };

	if ( id.has_value() ) co_return co_await getMime( id.value(), db );

	co_return std::unexpected( id.error() );
}

} // namespace idhan::mime