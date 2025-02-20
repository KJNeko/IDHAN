//
// Created by kj16609 on 11/14/24.
//
#pragma once
#include "drogon/HttpController.h"
#include "helpers/ResponseCallback.hpp"

namespace idhan::api
{

class IDHANFileAPI : public drogon::HttpController< IDHANFileAPI >
{
	drogon::Task< drogon::HttpResponsePtr > importFile( const drogon::HttpRequestPtr request );

	//! Creates a new record in the database. Responds with the record id in a json format.
	drogon::Task< drogon::HttpResponsePtr > createRecord( const drogon::HttpRequestPtr request );

  public:

	METHOD_LIST_BEGIN

	ADD_METHOD_TO( IDHANFileAPI::createRecord, "/file/create" );

	// tags
	// ADD_METHOD_TO( IDHANFileAPI::addTag, "/file/{file_id}/tags/add" );
	// ADD_METHOD_TO( IDHANFileAPI::removeTag, "/file/{file_id}/tags/remove" );
	// ADD_METHOD_TO( IDHANFileAPI::tagList, "/file/{file_id}/tags" );

	// info
	// ADD_METHOD_TO( IDHANFileAPI::info, "/file/{file_id}/info" );

	// file retrevial
	// ADD_METHOD_TO( IDHANFileAPI::file, "/file/{file_id}" );
	// ADD_METHOD_TO( IDHANFileAPI::thumbnail, "/file/{file_id}/thumbnail" );

	ADD_METHOD_TO( IDHANFileAPI::importFile, "/file/import" );

	METHOD_LIST_END
};
} // namespace idhan::api