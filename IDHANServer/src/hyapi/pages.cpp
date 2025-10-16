//
// Created by kj16609 on 10/15/25.
//

#include "HyAPI.hpp"
#include "hydrus/ClientConstants_gen.hpp"
#include "hydrus/ClientGUIPagesCore_gen.hpp"

namespace idhan::hyapi
{

drogon::Task< drogon::HttpResponsePtr > HydrusAPI::getPages( drogon::HttpRequestPtr request )
{
	Json::Value json {};

	// Dumy page to get hydrui to stop eating shit
	json[ "pages" ] = Json::Value( Json::objectValue );

	Json::Value fake_page {};
	fake_page[ "name" ] = "IDHAN Fake page";
	fake_page[ "page_key" ] = "cunny";
	fake_page[ "page_state" ] = hydrus::gen_constants::PAGE_STATE_NORMAL;
	fake_page[ "page_type" ] = hydrus::gen_constants::PAGE_TYPE_PAGE_OF_PAGES;
	fake_page[ "is_media_page" ] = false;
	fake_page[ "selected" ] = true;

	json[ "pages" ] = fake_page;

	co_return drogon::HttpResponse::newHttpJsonResponse( json );
}

} // namespace idhan::hyapi