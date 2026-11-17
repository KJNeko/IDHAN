#include "downloaderHosts.hpp"

#include "downloader/DatabaseParserHosts.hpp"
#include "downloader/DownloadSessionManager.hpp"
#include "idhan/http/routing/ReflectedRoutes.hpp"

namespace idhan::httpapi
{

static http::JsonResponse< std::vector< downloader::ParserHost > > parserHostsResponse(
	std::vector< downloader::ParserHost > hosts )
{
	return http::JsonResponse< std::vector< downloader::ParserHost > > { std::move( hosts ) };
}

http::HTTPTask< http::JsonResult< std::vector< downloader::ParserHost > > > listParserHosts(
	db::DBContext& db,
	http::RequestContext )
{
	co_return parserHostsResponse( co_await downloader::DatabaseParserHosts::list( db ) );
}

http::HTTPTask< http::JsonResult< std::vector< downloader::ParserHost > > > setParserHost(
	db::DBContext& db,
	http::JsonBody< SetParserHostBody > body )
{
	const auto& host { body.value.host };
	const auto& parser_id { body.value.parser };

	const auto host_e { downloader::normalizeParserHost( host ) };
	if ( !host_e.has_value() ) co_return http::Response::error( 400, host_e.error() );

	auto& manager { downloader::downloadSessionManager() };
	const auto options_e { co_await manager.downloadOptions() };
	if ( !options_e.has_value() ) co_return http::Response::error( 503, options_e.error() );

	bool known {};
	for ( const auto& candidate : options_e.value()[ "parsers" ] )
		if ( candidate[ "id" ].asString() == parser_id ) known = true;

	if ( !known ) co_return http::Response::error( 400, "Unknown parser" );

	const auto saved {
		co_await downloader::DatabaseParserHosts::upsert( db, { host_e.value(), std::string { parser_id }, false } )
	};
	const auto applied_e { co_await manager.setParserHost( saved.host, saved.parser ) };
	if ( !applied_e.has_value() ) co_return http::Response::error( 503, applied_e.error() );

	co_return parserHostsResponse( { saved } );
}

http::HTTPTask< http::JsonResult< DownloaderDeletedResponse > > removeParserHost(
	db::DBContext& db,
	http::Query< RemoveParserHostQuery > query )
{
	const auto normalized_e { downloader::normalizeParserHost( query.value.host ) };
	if ( !normalized_e.has_value() ) co_return http::Response::error( 400, normalized_e.error() );

	co_await downloader::DatabaseParserHosts::erase( db, normalized_e.value() );
	downloader::downloadSessionManager().removeParserHost( normalized_e.value() );
	co_return http::JsonResponse< DownloaderDeletedResponse > {};
}

} // namespace idhan::httpapi
