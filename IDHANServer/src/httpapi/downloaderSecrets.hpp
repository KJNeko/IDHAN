#pragma once

#include "DownloaderResponses.hpp"
#include "IDHANTypes.hpp"
#include "idhan/http/routing/ReflectedRoutes.hpp"
#include "idhan/http/routing/RouteSpec.hpp"

namespace idhan::db
{
class DBContext;
}

namespace idhan::httpapi
{

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::GET, "/downloader/secrets"}]]
[[= http::Group{"Downloader"}]]
http::HTTPTask< http::JsonResult< DownloaderSecrets > > listSecrets( [[maybe_unused]] http::RequestContext context );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::POST, "/downloader/secrets"}]]
[[= http::Group{"Downloader"}]]
http::HTTPTask< http::JsonResult< DownloaderSecrets > > setSecrets( http::JsonBody< DownloaderSecrets > body );
// clang-format on

} // namespace idhan::httpapi
