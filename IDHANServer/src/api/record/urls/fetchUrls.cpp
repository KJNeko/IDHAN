#include <expected>

#include "IDHANTypes.hpp"
#include "api/RecordAPI.hpp"
#include "urls.hpp"

namespace idhan
{

namespace api
{
drogon::Task< drogon::HttpResponsePtr > RecordAPI::fetchUrls(
	[[maybe_unused]] drogon::HttpRequestPtr request,
	const RecordID record_id )
{
	const auto db { drogon::app().getDbClient() };
	const auto urls { co_await fetchUrlsJson( record_id, db ) };

	if ( !urls ) co_return urls.error();

	co_return drogon::HttpResponse::newHttpJsonResponse( urls.value() );
}
} // namespace api

namespace helpers
{

ExpectedTask< std::vector< RecordUrl > > fetchUrlsDetailed( const RecordID record_id, DbClientPtr db )
{
	const auto url_maps { co_await db->execSqlCoro(
		"SELECT url, url_domain FROM url_mappings JOIN urls USING (url_id) JOIN url_domains USING (url_domain_id) "
		"WHERE record_id = $1",
		record_id ) };

	std::vector< RecordUrl > urls {};
	urls.reserve( url_maps.size() );

	for ( const auto& row : url_maps )
	{
		urls.emplace_back( row[ "url" ].as< std::string >(), row[ "url_domain" ].as< std::string >() );
	}

	co_return urls;
}

ExpectedTask< std::vector< std::string > > fetchUrlsStrings( const RecordID record_id, DbClientPtr db )
{
	const auto detailed { co_await fetchUrlsDetailed( record_id, db ) };

	if ( !detailed ) co_return std::unexpected( detailed.error() );

	std::vector< std::string > urls {};
	urls.reserve( detailed.value().size() );

	for ( const auto& entry : detailed.value() ) urls.emplace_back( entry.url );

	co_return urls;
}

ExpectedTask< Json::Value > fetchUrlsJson( const RecordID record_id, DbClientPtr db )
{
	const auto urls { co_await fetchUrlsStrings( record_id, db ) };

	if ( !urls ) co_return std::unexpected( urls.error() );

	Json::Value json { Json::ValueType::arrayValue };

	for ( const auto& url : urls.value() ) json.append( url );
	co_return json;
}
} // namespace helpers

} // namespace idhan
