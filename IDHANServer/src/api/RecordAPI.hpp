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

#include "APIAuth.hpp"
#include "IDHANTypes.hpp"
#include "db/dbTypes.hpp"
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
	drogon::Task< drogon::HttpResponsePtr > removeMultipleTags( drogon::HttpRequestPtr request );
	drogon::Task< drogon::HttpResponsePtr > listTags( drogon::HttpRequestPtr request, RecordID record_id );
	drogon::Task< drogon::HttpResponsePtr > listActiveTags( drogon::HttpRequestPtr request, RecordID record_id );
	drogon::Task< drogon::HttpResponsePtr > listActiveTagsVerbose( drogon::HttpRequestPtr request, RecordID record_id );

	drogon::Task< drogon::HttpResponsePtr > searchHash( drogon::HttpRequestPtr request );

	drogon::Task< drogon::HttpResponsePtr > fetchFile( drogon::HttpRequestPtr request, RecordID record_id );

	drogon::Task< drogon::HttpResponsePtr > fetchThumbnail( drogon::HttpRequestPtr request, RecordID record_id );

	drogon::Task< drogon::HttpResponsePtr > fetchInfo( drogon::HttpRequestPtr request, RecordID record_id );
	drogon::Task< drogon::HttpResponsePtr > parseFile( drogon::HttpRequestPtr request, RecordID record_id );

	drogon::Task< drogon::HttpResponsePtr > fetchUrls( drogon::HttpRequestPtr request, RecordID record_id );
	drogon::Task< drogon::HttpResponsePtr > addUrls( drogon::HttpRequestPtr request, RecordID record_id );
	drogon::Task< drogon::HttpResponsePtr > removeUrls( drogon::HttpRequestPtr request, RecordID record_id );

	drogon::Task< drogon::HttpResponsePtr > getRandomActiveRecord( drogon::HttpRequestPtr request );

	drogon::Task< drogon::HttpResponsePtr > getNotes( drogon::HttpRequestPtr request, RecordID record_id );
	drogon::Task< drogon::HttpResponsePtr > addNote( drogon::HttpRequestPtr request, RecordID record_id );
	drogon::Task< drogon::HttpResponsePtr > removeNote(
		drogon::HttpRequestPtr request,
		RecordID record_id,
		NoteID note_id );

  public:

	METHOD_LIST_BEGIN

	ADD_METHOD_TO( RecordAPI::getNotes, "/records/{record_id}/notes", drogon::Get, IDHANAPIAuthName );
	ADD_METHOD_TO( RecordAPI::addNote, "/records/{record_id}/add_note", drogon::Post, IDHANAPIAuthName );
	ADD_METHOD_TO( RecordAPI::removeNote, "/records/{record_id}/remove_note/{note_id}", drogon::Delete, IDHANAPIAuthName );

	ADD_METHOD_TO( RecordAPI::createRecord, "/records/create", drogon::Post, IDHANAPIAuthName );

	ADD_METHOD_TO( RecordAPI::fetchUrls, "/records/{record_id}/urls", drogon::Get, IDHANAPIAuthName );
	ADD_METHOD_TO( RecordAPI::addUrls, "/records/{record_id}/urls/add", drogon::Post, IDHANAPIAuthName );
	ADD_METHOD_TO( RecordAPI::removeUrls, "/records/{record_id}/urls/remove", drogon::Post, IDHANAPIAuthName );

	// tags
	ADD_METHOD_TO( RecordAPI::addMultipleTags, "/records/tags/add", drogon::Post, IDHANAPIAuthName );
	ADD_METHOD_TO( RecordAPI::removeMultipleTags, "/records/tags/remove", drogon::Post, IDHANAPIAuthName );

	ADD_METHOD_TO( RecordAPI::addTags, "/records/{record_id}/tags/add", drogon::Post, IDHANAPIAuthName );
	ADD_METHOD_TO( RecordAPI::removeTags, "/records/{record_id}/tags/remove", drogon::Post, IDHANAPIAuthName );
	ADD_METHOD_TO( RecordAPI::listTags, "/records/{record_id}/tags", drogon::Get, IDHANAPIAuthName );
	ADD_METHOD_TO( RecordAPI::listActiveTags, "/records/{record_id}/tags/active", drogon::Get, IDHANAPIAuthName );
	ADD_METHOD_TO( RecordAPI::listActiveTagsVerbose, "/records/{record_id}/tags/active/verbose", drogon::Get, IDHANAPIAuthName );

	ADD_METHOD_TO( RecordAPI::searchHash, "/records/search", drogon::Get, IDHANAPIAuthName );

	ADD_METHOD_TO( RecordAPI::getRandomActiveRecord, "/records/random", drogon::Get, IDHANAPIAuthName );

	ADD_METHOD_TO( RecordAPI::fetchThumbnail, "/records/{record_id}/thumbnail", drogon::Get, IDHANAPIAuthName );
	ADD_METHOD_TO( RecordAPI::fetchFile, "/records/{record_id}/file", drogon::Get, drogon::Head, IDHANAPIAuthName );
	ADD_METHOD_TO( RecordAPI::fetchFile, "/records/{record_id}", drogon::Get, drogon::Head, IDHANAPIAuthName );
	ADD_METHOD_TO( RecordAPI::fetchInfo, "/records/{record_id}/info", drogon::Get, IDHANAPIAuthName );
	ADD_METHOD_TO( RecordAPI::fetchInfo, "/records/{record_id}/metadata", drogon::Get, IDHANAPIAuthName );
	ADD_METHOD_TO( RecordAPI::parseFile, "/records/{record_id}/metadata/scan", drogon::Post, IDHANAPIAuthName );

	METHOD_LIST_END
};
} // namespace idhan::api
