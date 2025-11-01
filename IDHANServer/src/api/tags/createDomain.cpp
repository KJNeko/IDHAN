//
// Created by kj16609 on 2/20/25.
//

#include "api/TagAPI.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "drogon/HttpResponse.h"
#include "drogon/utils/coroutine.h"
#include "fgl/defines.hpp"
#include "logging/log.hpp"

namespace idhan::api
{

drogon::Task< std::optional< Json::Value > >
	getTagDomainInfoJson( const TagDomainID tag_domain_id, const DbClientPtr db )
{
	const auto search { co_await db->execSqlCoro(
		"SELECT tag_domain_id, domain_name FROM tag_domains WHERE tag_domain_id = $1", tag_domain_id ) };

	if ( search.empty() ) co_return std::nullopt;

	Json::Value out_json {};

	out_json[ "tag_domain_id" ] = static_cast< Json::UInt64 >( search[ 0 ][ 0 ].as< TagDomainID >() );
	out_json[ "domain_name" ] = search[ 0 ][ 1 ].as< std::string >();

	co_return out_json;
}

drogon::Task< drogon::HttpResponsePtr > TagAPI::createTagDomain( drogon::HttpRequestPtr request )
{
	const auto json_obj { request->getJsonObject() };

	if ( json_obj == nullptr )
	{
		co_return createBadRequest( "No valid json input" );
	}

	const auto json { *json_obj };

	const auto name { json[ "name" ] };

	auto db { drogon::app().getDbClient() };

	if ( name.isString() )
	{
		const auto search {
			co_await db->execSqlCoro( "SELECT tag_domain_id FROM tag_domains WHERE domain_name = $1", name.asString() )
		};

		if ( !search.empty() ) [[unlikely]]
		{
			const auto tag_domain_id { search[ 0 ][ 0 ].as< TagDomainID >() };

			if ( std::optional< Json::Value > out_json { co_await getTagDomainInfoJson( tag_domain_id, db ) } )
			{
				auto response { drogon::HttpResponse::newHttpJsonResponse( out_json.value() ) };
				response->setStatusCode( drogon::k409Conflict );

				co_return response;
			}

			co_return createInternalError( "Error getting tag domain info for domain {}", tag_domain_id );
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
		co_return drogon::HttpResponse::
			newHttpResponse( drogon::HttpStatusCode::k400BadRequest, drogon::ContentType::CT_NONE );
	}

	FGL_UNREACHABLE();
}

drogon::Task< drogon::HttpResponsePtr > TagAPI::getTagDomains( [[maybe_unused]] drogon::HttpRequestPtr request )
{
	auto db { drogon::app().getDbClient() };

	const auto search { co_await db->execSqlCoro( "SELECT tag_domain_id, domain_name FROM tag_domains" ) };

	Json::Value out_json {};
	// Done to make the result an empty array instead of null in the case of no domains
	out_json.resize( 0 );

	for ( const auto& row : search )
	{
		const auto info { co_await getTagDomainInfoJson( row[ 0 ].as< TagDomainID >(), db ) };

		if ( !info )
		{
			co_return createInternalError(
				"Failed to get info for tag domain {} despite it existing", row[ 0 ].as< std::string >() );
		}

		out_json.append( *info );
	}

	co_return drogon::HttpResponse::newHttpJsonResponse( out_json );
}

drogon::Task< drogon::HttpResponsePtr > TagAPI::
	getTagDomainInfo( [[maybe_unused]] drogon::HttpRequestPtr request, const TagDomainID tag_domain_id )
{
	auto db { drogon::app().getDbClient() };

	const auto search {
		co_await db->execSqlCoro( "SELECT tag_domain_id FROM tag_domains WHERE tag_domain_id = $1", tag_domain_id )
	};

	if ( search.empty() )
	{
		co_return createBadRequest( "Domain id {} does not exists", tag_domain_id );
	}

	const auto info { co_await getTagDomainInfoJson( tag_domain_id, db ) };

	if ( !info )
	{
		co_return createInternalError( "Failed to get info for tag domain {} despite it existing", tag_domain_id );
	}

	co_return drogon::HttpResponse::newHttpJsonResponse( *info );
}

drogon::Task< drogon::HttpResponsePtr > TagAPI::
	deleteTagDomain( [[maybe_unused]] drogon::HttpRequestPtr request, const TagDomainID tag_domain_id )
{
	auto db { drogon::app().getDbClient() };
	const auto search { co_await db->execSqlCoro( "DELETE FROM tag_domains WHERE tag_domain_id = $1", tag_domain_id ) };

	if ( search.empty() ) co_return createBadRequest( "Failed to find tag domain by id {}", tag_domain_id );

	Json::Value out_json {};

	out_json[ "tag_domain_id" ] = static_cast< Json::Value::UInt >( search[ 0 ][ 0 ].as< TagDomainID >() );

	co_return drogon::HttpResponse::newHttpJsonResponse( out_json );
}

} // namespace idhan::api
