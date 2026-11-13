#pragma once

#include <vector>

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

struct CreateSessionBody
{
	std::string name;
};

struct UploadParserBody
{
	std::string filename;
	std::string source;
	http::possiblyMissing< bool > replace;
};

struct SubmitUrlBody
{
	std::string url;
};

struct SubmitUrlSessionBodyOptions
{
	http::possiblyMissing< bool > force_import;
	http::possiblyMissing< std::map< std::string, std::map< std::string, ParserOptionValue > > > parsers;
};

struct SubmitUrlSessionBody
{
	http::possiblyMissing< std::string > name;
	std::string url;
	http::possiblyMissing< SubmitUrlSessionBodyOptions > options;
};

struct ListUrlsQuery
{
	http::possiblyMissing< std::string > flatten;
};

struct ListErrorsQuery
{
	http::possiblyMissing< std::string > status;
};

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::GET, "/download_sessions"}]]
[[= http::Group{"Downloader"}]]
http::HTTPTask< http::JsonResult< std::vector< DownloadSessionResponse > > > listSessions( db::DBContext& db, [[maybe_unused]] http::RequestContext context );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::POST, "/download_sessions"}]]
[[= http::Group{"Downloader"}]]
[[= http::Output< http::ErrorResponse >{409}]]
http::HTTPTask< http::JsonResult< DownloadSessionResponse, 201 > > createSession( db::DBContext& db, http::JsonBody< CreateSessionBody > body );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::GET, "/download_sessions/{session_id}"}]]
[[= http::Group{"Downloader"}]]
[[= http::Validation< http::validation::PositiveInteger< DownloadSessionID > >{"session_id"}]]
[[= http::Output< http::ErrorResponse >{404}]]
http::HTTPTask< http::JsonResult< DownloadSessionResponse > > getSession( db::DBContext& db, http::RequestContext context );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::DELETE, "/download_sessions/{session_id}"}]]
[[= http::Group{"Downloader"}]]
[[= http::Validation< http::validation::PositiveInteger< DownloadSessionID > >{"session_id"}]]
[[= http::Output< http::ErrorResponse >{404}]]
http::HTTPTask< http::JsonResult< DownloaderDeletedResponse > > destroySession( db::DBContext& db, http::RequestContext context );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::GET, "/download_sessions/options"}]]
[[= http::Group{"Downloader"}]]
[[= http::Output< http::ErrorResponse >{503}]]
http::HTTPTask< http::JsonResult< DownloadOptionsResponse > > downloadOptions( db::DBContext&, http::RequestContext );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::POST, "/download_sessions/parsers"}]]
[[= http::Group{"Downloader"}]]
[[= http::Output< http::ErrorResponse >{409}]]
http::HTTPTask< http::JsonResult< ParserInstalledResponse > > uploadParser( db::DBContext&, http::JsonBody< UploadParserBody > body );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::GET, "/download_sessions/parsers/{filename}"}]]
[[= http::Group{"Downloader"}]]
[[= http::Output< http::ErrorResponse >{404}]]
http::HTTPTask< http::EncodedResult< std::string, "text/javascript" > > downloadParser( db::DBContext&, http::RequestContext context );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::GET, "/download_sessions/parsers"}]]
[[= http::Group{"Downloader"}]]
[[= http::Output< http::ErrorResponse >{503}]]
http::HTTPTask< http::JsonResult< ParserListResponse > > listParsers( db::DBContext&, http::RequestContext );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::POST, "/download_sessions/{session_id}/urls"}]]
[[= http::Group{"Downloader"}]]
[[= http::Validation< http::validation::PositiveInteger< DownloadSessionID > >{"session_id"}]]
[[= http::Output< http::ErrorResponse >{404}]]
http::HTTPTask< http::JsonResult< SubmittedDownloadUrlResponse, 201 > > submitUrl( db::DBContext& db, http::RequestContext context, http::JsonBody< SubmitUrlBody > body );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::POST, "/download_sessions/urls"}]]
[[= http::Group{"Downloader"}]]
[[= http::Output< http::ErrorResponse >{409}]]
[[= http::Output< http::ErrorResponse >{503}]]
http::HTTPTask< http::JsonResult< SubmittedDownloadSessionResponse, 201 > > submitUrlSession( db::DBContext& db, http::JsonBody< SubmitUrlSessionBody > body );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::GET, "/download_sessions/{session_id}/urls"}]]
[[= http::Group{"Downloader"}]]
[[= http::Validation< http::validation::PositiveInteger< DownloadSessionID > >{"session_id"}]]
[[= http::Output< http::ErrorResponse >{404}]]
http::HTTPTask< http::JsonResult< DownloadUrlListResponse > > listUrls( db::DBContext& db, http::RequestContext context, http::Query< ListUrlsQuery > query );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::POST, "/download_sessions/{session_id}/urls/{url_id}/retry"}]]
[[= http::Group{"Downloader"}]]
[[= http::Validation< http::validation::PositiveInteger< DownloadSessionID > >{"session_id"}]]
[[= http::Validation< http::validation::PositiveInteger< DownloadSessionUrlID > >{"url_id"}]]
[[= http::Output< http::ErrorResponse >{404}]]
[[= http::Output< http::ErrorResponse >{409}]]
http::HTTPTask< http::JsonResult< DownloadUrlNodeResponse > > retryUrl( db::DBContext& db, http::RequestContext context );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::GET, "/download_sessions/{session_id}/records"}]]
[[= http::Group{"Downloader"}]]
[[= http::Validation< http::validation::PositiveInteger< DownloadSessionID > >{"session_id"}]]
[[= http::Output< http::ErrorResponse >{404}]]
http::HTTPTask< http::JsonResult< DownloadSessionRecordsResponse > > listRecords( db::DBContext& db, http::RequestContext context );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::GET, "/download_sessions/{session_id}/errors"}]]
[[= http::Group{"Downloader"}]]
[[= http::Validation< http::validation::PositiveInteger< DownloadSessionID > >{"session_id"}]]
[[= http::Output< http::ErrorResponse >{404}]]
http::HTTPTask< http::JsonResult< DownloadSessionErrorsResponse > > listErrors( db::DBContext& db, http::RequestContext context, http::Query< ListErrorsQuery > query );
// clang-format on

} // namespace idhan::httpapi
