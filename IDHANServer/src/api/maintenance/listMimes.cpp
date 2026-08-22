#include "api/APIMaintenance.hpp"
#include "drogon/HttpAppFramework.h"

namespace idhan::api
{

//! Every mime id the table carries, mapped to the name it reports. Many-to-one: two ids may share a
//! name (a Pixiv Ugoira reports "application/zip"), so the map cannot be inverted.
drogon::Task< drogon::HttpResponsePtr > APIMaintenance::listMimes( [[maybe_unused]] drogon::HttpRequestPtr request )
{
	const auto db { drogon::app().getDbClient() };

	const auto rows { co_await db->execSqlCoro( "SELECT mime_id, name FROM mime ORDER BY mime_id" ) };

	Json::Value json { Json::objectValue };

	for ( const auto& row : rows )
		json[ std::to_string( row[ "mime_id" ].as< MimeID >() ) ] = row[ "name" ].as< std::string >();

	co_return drogon::HttpResponse::newHttpJsonResponse( std::move( json ) );
}

} // namespace idhan::api
