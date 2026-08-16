#pragma once

#include <drogon/HttpController.h>

#include "api/APIAuth.hpp"

namespace idhan::api
{

//! Optional server-side copy of browser-owned WebUI layouts; ids are client-generated UUIDs.
class LayoutAPI : public drogon::HttpController< LayoutAPI >
{
	//! Metadata only; documents are fetched lazily.
	static drogon::Task< drogon::HttpResponsePtr > listLayouts( drogon::HttpRequestPtr req );

	static drogon::Task< drogon::HttpResponsePtr > createLayout( drogon::HttpRequestPtr req );

	static drogon::Task< drogon::HttpResponsePtr > getLayout( drogon::HttpRequestPtr req, std::string id );

	//! Upsert; 409 only on name collision with a different layout id.
	static drogon::Task< drogon::HttpResponsePtr > putLayout( drogon::HttpRequestPtr req, std::string id );

	static drogon::Task< drogon::HttpResponsePtr > deleteLayout( drogon::HttpRequestPtr req, std::string id );

  public:

	METHOD_LIST_BEGIN

	ADD_METHOD_TO( LayoutAPI::listLayouts, "/layouts", drogon::Get, IDHANAPIAuthName );
	ADD_METHOD_TO( LayoutAPI::createLayout, "/layouts", drogon::Post, IDHANAPIAuthName );
	ADD_METHOD_TO( LayoutAPI::getLayout, "/layouts/{id}", drogon::Get, IDHANAPIAuthName );
	ADD_METHOD_TO( LayoutAPI::putLayout, "/layouts/{id}", drogon::Put, IDHANAPIAuthName );
	ADD_METHOD_TO( LayoutAPI::deleteLayout, "/layouts/{id}", drogon::Delete, IDHANAPIAuthName );

	METHOD_LIST_END
};

} // namespace idhan::api
