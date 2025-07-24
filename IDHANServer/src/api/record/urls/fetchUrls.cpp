//
// Created by kj16609 on 7/24/25.
//

#include <expected>

#include "IDHANTypes.hpp"
#include "api/IDHANRecordAPI.hpp"
#include "urls.hpp"

namespace idhan
{

namespace api
{
drogon::Task< drogon::HttpResponsePtr > IDHANRecordAPI::
	fetchUrls( [[maybe_unused]] drogon::HttpRequestPtr request, const RecordID record_id )
{
	const auto db { drogon::app().getFastDbClient() };
	const auto urls { co_await fetchUrlsJson( record_id, db ) };

	if ( !urls.has_value() ) co_return urls.error();

	co_return drogon::HttpResponse::newHttpJsonResponse( urls.value() );
}
} // namespace api

namespace helpers
{

drogon::Task< std::expected< std::vector< std::string >, drogon::HttpResponsePtr > >
	fetchUrlsStrings( const RecordID record_id, drogon::orm::DbClientPtr db )
{
	const auto url_maps { co_await db->execSqlCoro(
		"SELECT url_id, url FROM url_mappings JOIN urls USING (url_id)  WHERE record_id = $1", record_id ) };

	std::vector< std::string > urls {};
	urls.reserve( url_maps.size() );

	for ( const auto& row : url_maps )
	{
		const auto url { row[ "url" ].as< std::string >() };
		urls.emplace_back( url );
	}

	co_return urls;
}

drogon::Task< std::expected< Json::Value, drogon::HttpResponsePtr > >
	fetchUrlsJson( const RecordID record_id, drogon::orm::DbClientPtr db )
{
	const auto urls { co_await fetchUrlsStrings( record_id, db ) };

	if ( !urls.has_value() ) co_return std::unexpected( urls.error() );

	Json::Value json { Json::ValueType::arrayValue };

	for ( const auto& url : urls.value() ) json.append( url );
	co_return json;
}
} // namespace helpers

} // namespace idhan
