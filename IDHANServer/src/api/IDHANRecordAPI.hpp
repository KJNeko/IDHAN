//
// Created by kj16609 on 11/14/24.
//
#pragma once
#include "IDHANTypes.hpp"
#include "drogon/HttpController.h"
#include "helpers/ResponseCallback.hpp"

namespace idhan::api
{

class IDHANRecordAPI : public drogon::HttpController< IDHANRecordAPI >
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

  public:

	METHOD_LIST_BEGIN

	ADD_METHOD_TO( IDHANRecordAPI::createRecord, "/records/create" );

	// tags
	ADD_METHOD_TO( IDHANRecordAPI::addMultipleTags, "/records/tags/add" );

	ADD_METHOD_TO( IDHANRecordAPI::addTags, "/records/{record_id}/tags/add" );
	ADD_METHOD_TO( IDHANRecordAPI::removeTags, "/records/{record_id}/tags/remove" );
	ADD_METHOD_TO( IDHANRecordAPI::listTags, "/records/{record_id}/tags" );

	ADD_METHOD_TO( IDHANRecordAPI::searchHash, "/records/search" );

	ADD_METHOD_TO( IDHANRecordAPI::fetchFile, "/records/{record_id}/file" );
	ADD_METHOD_TO( IDHANRecordAPI::fetchFile, "/records/{record_id}" );

	METHOD_LIST_END
};
} // namespace idhan::api