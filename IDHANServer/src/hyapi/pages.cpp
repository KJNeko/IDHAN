#include "HyAPI.hpp"
#include "hydrus/ClientConstants_gen.hpp"
#include "hydrus/ClientGUIPagesCore_gen.hpp"

namespace idhan::hyapi
{

drogon::Task< drogon::HttpResponsePtr > HydrusAPI::getPages( [[maybe_unused]] drogon::HttpRequestPtr request )
{
	Json::Value json {};

	json[ "pages" ] = Json::Value( Json::arrayValue );

	Json::Value fake_page {};
	fake_page[ "name" ] = "IDHAN Fake page";
	fake_page[ "page_key" ] = "696468616e2066616b65207061676520666f7220687964727573206170690000";
	fake_page[ "page_state" ] = hydrus::gen_constants::PAGE_STATE_NORMAL;
	fake_page[ "page_type" ] = hydrus::gen_constants::PAGE_TYPE_PAGE_OF_PAGES;
	fake_page[ "is_media_page" ] = false;
	fake_page[ "selected" ] = true;

	json[ "pages" ].append( std::move( fake_page ) );

	co_return drogon::HttpResponse::newHttpJsonResponse( json );
}

} // namespace idhan::hyapi
