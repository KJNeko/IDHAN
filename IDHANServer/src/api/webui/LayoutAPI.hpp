//
// Created by kj16609 on 7/18/26.
//
#pragma once

#include <drogon/HttpController.h>

#include "api/APIAuth.hpp"

namespace idhan::api
{

//! CRUD for WebUI named layouts pushed to the server (M5). Layouts live primarily in the browser;
//! these endpoints are the optional server-side copy used to move a layout between browsers. Identity
//! is the client-generated uuid, so a push (PUT) is an upsert. There is no ownership or user system.
class LayoutAPI : public drogon::HttpController< LayoutAPI >
{
	//! Lists stored layouts as metadata only (id, name, schema_ver, timestamps) — never the documents,
	//! which can be large. The client pulls a full document lazily with getLayout.
	static drogon::Task< drogon::HttpResponsePtr > listLayouts( drogon::HttpRequestPtr req );

	//! Strict create from a full LayoutDocument body. 409 if the id already exists or the name collides
	//! (case-insensitive) with a different layout.
	static drogon::Task< drogon::HttpResponsePtr > createLayout( drogon::HttpRequestPtr req );

	//! Returns the full stored LayoutDocument for @p id, or 404.
	static drogon::Task< drogon::HttpResponsePtr > getLayout( drogon::HttpRequestPtr req, std::string id );

	//! Push-to-server: upsert the document at @p id. 409 only if the name collides with a *different*
	//! layout id.
	static drogon::Task< drogon::HttpResponsePtr > putLayout( drogon::HttpRequestPtr req, std::string id );

	//! Deletes the layout at @p id. Returns { deleted: <bool> }.
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
