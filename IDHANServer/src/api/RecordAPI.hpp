//
// Created by kj16609 on 11/14/24.
//
#pragma once

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Weffc++"
#pragma GCC diagnostic ignored "-Wredundant-tags"
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wnoexcept"
#pragma GCC diagnostic ignored "-Wredundant-decls"
#pragma GCC diagnostic ignored "-Wuseless-cast"
#pragma GCC diagnostic ignored "-Wnoexcept"
#pragma GCC diagnostic ignored "-Wswitch-enum"
#pragma GCC diagnostic ignored "-Wshadow"
#include "drogon/HttpController.h"
#pragma GCC diagnostic pop

#include "IDHANTypes.hpp"
#include "helpers/ResponseCallback.hpp"

namespace idhan::api
{

class RecordAPI : public drogon::HttpController< RecordAPI >
{
	// drogon::Task< drogon::HttpResponsePtr > importFile( const drogon::HttpRequestPtr request );

	//! Creates a new record in the database. Responds with the record id in a json format.
	drogon::Task< drogon::HttpResponsePtr > createRecord( drogon::HttpRequestPtr request );

	drogon::Task< drogon::HttpResponsePtr > addTags( drogon::HttpRequestPtr request, RecordID record_id );
	drogon::Task< drogon::HttpResponsePtr > addMultipleTags( drogon::HttpRequestPtr request );

	drogon::Task< drogon::HttpResponsePtr > removeTags( drogon::HttpRequestPtr request, RecordID record_id );
	drogon::Task< drogon::HttpResponsePtr > listTags( drogon::HttpRequestPtr request, RecordID record_id );

	drogon::Task< drogon::HttpResponsePtr > searchHash( drogon::HttpRequestPtr request );

	drogon::Task< drogon::HttpResponsePtr > fetchFile( drogon::HttpRequestPtr request, RecordID record_id );

	drogon::Task< drogon::HttpResponsePtr > fetchThumbnail( drogon::HttpRequestPtr request, RecordID record_id );

	drogon::Task< drogon::HttpResponsePtr > fetchInfo( drogon::HttpRequestPtr request, RecordID record_id );
	drogon::Task< drogon::HttpResponsePtr > parseFile( drogon::HttpRequestPtr request, RecordID record_id );

	drogon::Task< drogon::HttpResponsePtr > fetchUrls( drogon::HttpRequestPtr request, RecordID record_id );
	drogon::Task< drogon::HttpResponsePtr > addUrls( drogon::HttpRequestPtr request, RecordID record_id );
	drogon::Task< drogon::HttpResponsePtr > removeUrls( drogon::HttpRequestPtr request, RecordID record_id );

  public:

	METHOD_LIST_BEGIN

	ADD_METHOD_TO( RecordAPI::createRecord, "/records/create" );

	ADD_METHOD_TO( RecordAPI::fetchUrls, "/records/{record_id}/urls" );
	ADD_METHOD_TO( RecordAPI::addUrls, "/records/{record_id}/urls/add" );
	ADD_METHOD_TO( RecordAPI::removeUrls, "/records/{record_id}/urls/remove" );

	// tags
	ADD_METHOD_TO( RecordAPI::addMultipleTags, "/records/tags/add" );

	ADD_METHOD_TO( RecordAPI::addTags, "/records/{record_id}/tags/add" );
	ADD_METHOD_TO( RecordAPI::removeTags, "/records/{record_id}/tags/remove" );
	ADD_METHOD_TO( RecordAPI::listTags, "/records/{record_id}/tags" );

	ADD_METHOD_TO( RecordAPI::searchHash, "/records/search" );

	ADD_METHOD_TO( RecordAPI::fetchThumbnail, "/records/{record_id}/thumbnail" );
	ADD_METHOD_TO( RecordAPI::fetchFile, "/records/{record_id}/file" );
	ADD_METHOD_TO( RecordAPI::fetchFile, "/records/{record_id}" );
	ADD_METHOD_TO( RecordAPI::fetchInfo, "/records/{record_id}/metadata" );
	ADD_METHOD_TO( RecordAPI::parseFile, "/records/{record_id}/metadata/scan" );

	METHOD_LIST_END
};
} // namespace idhan::api