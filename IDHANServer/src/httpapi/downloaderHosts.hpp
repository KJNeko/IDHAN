#pragma once

#include "DownloaderResponses.hpp"
#include "IDHANDownloader/parsers/ParserHostPersistence.hpp"
#include "IDHANTypes.hpp"
#include "idhan/http/routing/ReflectedRoutes.hpp"
#include "idhan/http/routing/RouteSpec.hpp"

namespace idhan::db
{
class DBContext;
}

namespace idhan::httpapi
{

struct SetParserHostBody
{
	std::string host;
	std::string parser;
};

struct RemoveParserHostQuery
{
	std::string host;
};

// clang-format off
[[= http::Get{"/downloader/hosts"}]]
[[= http::Group{"Downloader"}]]
http::HTTPTask< http::JsonResult< std::vector< downloader::ParserHost > > > listParserHosts( db::DBContext& db, http::RequestContext );
// clang-format on

// clang-format off
[[= http::Post{"/downloader/hosts"}]]
[[= http::Group{"Downloader"}]]
[[= http::Output< http::ErrorResponse >{503}]]
http::HTTPTask< http::JsonResult< std::vector< downloader::ParserHost > > > setParserHost( db::DBContext& db, http::JsonBody< SetParserHostBody > body );
// clang-format on

// clang-format off
[[= http::Delete{"/downloader/hosts"}]]
[[= http::Group{"Downloader"}]]
http::HTTPTask< http::JsonResult< DownloaderDeletedResponse > > removeParserHost( db::DBContext& db, http::Query< RemoveParserHostQuery > query );
// clang-format on

} // namespace idhan::httpapi
