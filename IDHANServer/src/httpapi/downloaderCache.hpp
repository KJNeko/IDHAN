#pragma once

#include "IDHANDownloader/parsers/ParserCacheSnapshot.hpp"
#include "idhan/http/routing/ReflectedRoutes.hpp"
#include "idhan/http/routing/RouteSpec.hpp"

namespace idhan::httpapi
{

// clang-format off
[[= http::Get{"/downloader/cache"}]]
[[= http::Group{"Downloader"}]]
http::HTTPTask< http::JsonResult< downloader::ParserCacheSnapshot > > parserCacheStats( http::RequestContext );
// clang-format on

} // namespace idhan::httpapi
