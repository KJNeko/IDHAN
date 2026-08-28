#include "api/TagAPI.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "drogon/HttpResponse.h"
#include "drogon/utils/coroutine.h"
#include "fgl/defines.hpp"
#include "logging/log.hpp"
#include "tags/tags.hpp"

namespace idhan::api
{

static Json::Value tagDomainInfoJson( const TagDomainInfo& domain )
{
	Json::Value json {};
	json[ "tag_domain_id" ] = static_cast< Json::UInt64 >( domain.id );
	json[ "domain_name" ] = domain.name;
	return json;
}

drogon::Task< std::optional< Json::Value > > getTagDomainInfoJson(
	const TagDomainID tag_domain_id,
	const DbClientPtr db )
{
	const auto domain { co_await findTagDomain( tag_domain_id, db ) };

	if ( !domain ) co_return std::nullopt;

	co_return tagDomainInfoJson( *domain );
}

drogon::Task< drogon::HttpResponsePtr > TagAPI::createTagDomain( drogon::HttpRequestPtr request )
{
	const auto json_obj { request->getJsonObject() };

	if ( json_obj == nullptr )
	{
		co_return createBadRequest( "No valid json input" );
	}

	const auto& json { *json_obj };

	// operator[] on a non-object root throws Json::LogicError, which would surface as a 500
	if ( !json.isObject() ) co_return createBadRequest( "Invalid json object. Expected object as root item" );

	const auto& name { json[ "name" ] };

	auto db { drogon::app().getDbClient() };

	if ( name.isString() )
	{
		const auto existing { co_await findTagDomain( name.asString(), db ) };

		if ( existing ) [[unlikely]]
		{
			log::debug( "Found existing tag domain with name '{}'", name.asString() );

			auto response { drogon::HttpResponse::newHttpJsonResponse( tagDomainInfoJson( *existing ) ) };
			response->setStatusCode( drogon::k409Conflict );

			co_return response;
		}

		const auto create { co_await db->execSqlCoro(
			"INSERT INTO tag_domains (domain_name) VALUES ($1) RETURNING tag_domain_id", name.asString() ) };

		if ( create.size() > 0 )
		{
			log::debug( "Created tag domain \'{}\' as id {}", name.asString(), create[ 0 ][ 0 ].as< TagDomainID >() );

			if ( auto info = co_await getTagDomainInfoJson( create[ 0 ][ 0 ].as< TagDomainID >(), db ) )
			{
				co_return drogon::HttpResponse::newHttpJsonResponse( *info );
			}
		}

		co_return createInternalError( "Error creating new domain with name {}", name.asString() );
	}
	else
	{
		log::error( "Failed to parse json" );
		co_return drogon::HttpResponse::newHttpResponse(
			drogon::HttpStatusCode::k400BadRequest, drogon::ContentType::CT_NONE );
	}

	FGL_UNREACHABLE();
}

drogon::Task< drogon::HttpResponsePtr > TagAPI::getTagDomains( [[maybe_unused]] drogon::HttpRequestPtr request )
{
	auto db { drogon::app().getDbClient() };

	const auto domains { co_await listTagDomains( db ) };

	Json::Value out_json {};
	out_json.resize( 0 );

	for ( const auto& domain : domains )
	{
		out_json.append( tagDomainInfoJson( domain ) );
	}

	co_return drogon::HttpResponse::newHttpJsonResponse( out_json );
}

drogon::Task< drogon::HttpResponsePtr > TagAPI::getTagDomainInfo(
	[[maybe_unused]] drogon::HttpRequestPtr request,
	const TagDomainID tag_domain_id )
{
	auto db { drogon::app().getDbClient() };

	const auto domain { co_await findTagDomain( tag_domain_id, db ) };

	if ( !domain )
	{
		co_return createNotFound( "Domain id {} does not exist", tag_domain_id );
	}

	Json::Value info {};
	info[ "tag_domain_id" ] = static_cast< Json::UInt64 >( domain->id );
	info[ "domain_name" ] = domain->name;

	co_return drogon::HttpResponse::newHttpJsonResponse( info );
}

drogon::Task< drogon::HttpResponsePtr > TagAPI::deleteTagDomain(
	[[maybe_unused]] drogon::HttpRequestPtr request,
	const TagDomainID tag_domain_id )
{
	auto db { drogon::app().getDbClient() };
	const auto search { co_await db->execSqlCoro(
		"DELETE FROM tag_domains WHERE tag_domain_id = $1 RETURNING tag_domain_id", tag_domain_id ) };

	if ( search.empty() ) co_return createNotFound( "Failed to find tag domain by id {}", tag_domain_id );

	Json::Value out_json {};

	out_json[ "tag_domain_id" ] = static_cast< Json::Value::UInt >( search[ 0 ][ 0 ].as< TagDomainID >() );

	co_return drogon::HttpResponse::newHttpJsonResponse( out_json );
}

} // namespace idhan::api
