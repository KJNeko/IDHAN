#include "Ui.hpp"

#include "paths.hpp"

namespace idhan
{

drogon::Task< drogon::HttpResponsePtr > Ui::index( drogon::HttpRequestPtr )
{
	if ( const auto path { getStaticPath() / "index.html" }; std::filesystem::exists( path ) )
		co_return drogon::HttpResponse::newFileResponse( path );

	co_return drogon::HttpResponse::newNotFoundResponse();
}

} // namespace idhan
