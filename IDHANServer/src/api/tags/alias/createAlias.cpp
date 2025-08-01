//
// Created by kj16609 on 3/11/25.
//

#include "api/IDHANTagAPI.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "api/helpers/helpers.hpp"
#include "fgl/defines.hpp"
#include "logging/log.hpp"

namespace idhan::api
{

drogon::Task< drogon::HttpResponsePtr > IDHANTagAPI::createTagAliases( drogon::HttpRequestPtr request )
{
	const auto json_obj { request->getJsonObject() };

	if ( json_obj == nullptr )
	{
		co_return createBadRequest( "No valid json object" );
	}

	const auto json { *json_obj };

	if ( !json.isArray() )
	{
		co_return createBadRequest( "Invalid json object. Expected array as root item" );
	}

	const auto db { drogon::app().getDbClient() };

	const auto tag_domain_id { helpers::getTagDomainID( request ) };

	if ( !tag_domain_id ) co_return tag_domain_id.error();

	if ( json.size() == 0 ) log::warn( "createAlias: Json array size was zero, Possible mistake?" );

	std::vector< drogon::orm::internal::SqlAwaiter > awaiters {};
	awaiters.reserve( json.size() );

	for ( const auto& item : json )
	{
		const auto& aliased { item[ "aliased_id" ] };
		const auto& alias { item[ "alias_id" ] };

		if ( !aliased.isIntegral() ) co_return createBadRequest( "Invalid aliased item: Must be in TagID form" );
		if ( !alias.isIntegral() ) co_return createBadRequest( "Invalid alias item: Must be in TagID form" );

		const TagID aliased_id { aliased.as< TagID >() };
		const TagID alias_id { alias.as< TagID >() };

		if ( aliased_id == alias_id )
			co_return createBadRequest( "Cannot alias a tag to itself {} == {}", aliased_id, alias_id );

		awaiters.emplace_back( db->execSqlCoro(
			"INSERT INTO tag_aliases (domain_id, aliased_id, alias_id) VALUES ($1, $2, $3) ON CONFLICT(domain_id, aliased_id) DO NOTHING",
			tag_domain_id.value(),
			aliased_id,
			alias_id ) );
	}

	Json::Value out_json {};

	for ( auto& awaiter : awaiters )
	{
		try
		{
			co_await awaiter;
		}
		catch ( drogon::orm::DrogonDbException& e )
		{
			log::error( "Failed to create alias: {}", e.base().what() );

			Json::Value value {};

			out_json.append( e.base().what() );

			co_return createBadRequest( "Failed to create alias: {}", e.base().what() );
		}
	}

	co_return drogon::HttpResponse::newHttpJsonResponse( out_json );
}

} // namespace idhan::api
