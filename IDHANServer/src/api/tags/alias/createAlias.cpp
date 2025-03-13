//
// Created by kj16609 on 3/11/25.
//

#include "api/IDHANTagAPI.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "api/helpers/helpers.hpp"
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
	auto transaction { db->newTransaction() };

	const auto tag_domain_id { helpers::getTagDomainID( request ) };

	if ( !tag_domain_id.has_value() ) co_return tag_domain_id.error();

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

		awaiters.emplace_back( transaction->execSqlCoro(
			"INSERT INTO tag_aliases (domain_id, aliased_id, alias_id) VALUES ($1, $2, $3) ON CONFLICT(domain_id, aliased_id, alias_id) DO NOTHING",
			tag_domain_id.value(),
			aliased_id,
			alias_id ) );
	}

	for ( auto& awaiter : awaiters ) co_await awaiter;

	co_return drogon::HttpResponse::newHttpResponse();
}

} // namespace idhan::api
