#pragma once
#include "APIAuth.hpp"
#include "IDHANTypes.hpp"
#include "drogon/HttpController.h"

namespace idhan::api
{

class FileRelationshipsAPI : public drogon::HttpController< FileRelationshipsAPI >
{
	drogon::Task< drogon::HttpResponsePtr > getRelationships( drogon::HttpRequestPtr request, RecordID record_id );
	drogon::Task< drogon::HttpResponsePtr > findSimilar( drogon::HttpRequestPtr request, RecordID record_id );
	drogon::Task< drogon::HttpResponsePtr > nextUndecided( drogon::HttpRequestPtr request );
	drogon::Task< drogon::HttpResponsePtr > setBetterDuplicate( drogon::HttpRequestPtr request );
	drogon::Task< drogon::HttpResponsePtr > addAlternative( drogon::HttpRequestPtr request );
	drogon::Task< drogon::HttpResponsePtr > clearRelationship( drogon::HttpRequestPtr request );
	drogon::Task< drogon::HttpResponsePtr > setUnrelated( drogon::HttpRequestPtr request );

  public:

	METHOD_LIST_BEGIN

	ADD_METHOD_TO(
		FileRelationshipsAPI::getRelationships,
		"/relationships/{record_id}",
		drogon::Get,
		IDHANAPIAuthName );

	ADD_METHOD_TO(
		FileRelationshipsAPI::findSimilar,
		"/relationships/{record_id}/similar",
		drogon::Get,
		IDHANAPIAuthName );

	ADD_METHOD_TO(
		FileRelationshipsAPI::nextUndecided,
		"/relationships/duplicates/undecided",
		drogon::Get,
		IDHANAPIAuthName );

	ADD_METHOD_TO(
		FileRelationshipsAPI::setBetterDuplicate,
		"/relationships/duplicates/add",
		drogon::Post,
		IDHANAPIAuthName );

	ADD_METHOD_TO(
		FileRelationshipsAPI::addAlternative,
		"/relationships/alternatives/add",
		drogon::Post,
		IDHANAPIAuthName );

	ADD_METHOD_TO( FileRelationshipsAPI::clearRelationship, "/relationships/clear", drogon::Post, IDHANAPIAuthName );

	ADD_METHOD_TO( FileRelationshipsAPI::setUnrelated, "/relationships/unrelated/add", drogon::Post, IDHANAPIAuthName );

	METHOD_LIST_END
};

} // namespace idhan::api
