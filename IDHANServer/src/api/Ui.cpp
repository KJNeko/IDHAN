//
// Created by kj16609 on 7/28/25.
//
#include "Ui.hpp"

#include "paths.hpp"

namespace idhan
{

drogon::Task< drogon::HttpResponsePtr > Ui::index( drogon::HttpRequestPtr )
{
	co_return drogon::HttpResponse::newFileResponse( getStaticPath() / "IDHANWebUI.html" );
}

} // namespace idhan
