//
// Created by kj16609 on 3/11/25.
//

#include "api/TagAPI.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "api/helpers/helpers.hpp"
#include "fgl/defines.hpp"
#include "logging/log.hpp"

namespace idhan::api
{

drogon::Task< drogon::HttpResponsePtr > TagAPI::createTagAliases( drogon::HttpRequestPtr request )
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

	const auto tag_domain_id { helpers::getTagDomainIDParameter( request ) };

	if ( !tag_domain_id ) co_return tag_domain_id.error();

	if ( json.size() == 0 ) log::warn( "createAlias: Json array size was zero, Possible mistake?" );

	std::vector< std::pair< TagID, TagID > > pairs {};
	pairs.reserve( json.size() );
	std::vector< TagID > referenced_tags {};
	referenced_tags.reserve( static_cast< std::size_t >( json.size() ) * 2 );

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

		pairs.emplace_back( aliased_id, alias_id );
		referenced_tags.emplace_back( aliased_id );
		referenced_tags.emplace_back( alias_id );
	}

	// unknown IDs would otherwise surface as FK-violation 500s
	const auto validation { co_await helpers::validateRelationshipIds(
		tag_domain_id.value(), std::move( referenced_tags ), db ) };
	if ( !validation ) co_return validation.error();

	for ( const auto& [ aliased_id, alias_id ] : pairs )
	{
		try
		{
			const auto insert_result { co_await db->execSqlCoro(
				"INSERT INTO tag_aliases (tag_domain_id, aliased_id, alias_id) VALUES "
				"($1, $2, $3) ON CONFLICT(tag_domain_id, aliased_id) DO NOTHING RETURNING alias_id",
				tag_domain_id.value(),
				aliased_id,
				alias_id ) };

			if ( insert_result.empty() )
			{
				// conflict: this tag already has an alias in this domain. DO NOTHING would
				// silently drop a request for a different target, so report that as a conflict
				const auto existing { co_await db->execSqlCoro(
					"SELECT alias_id FROM tag_aliases WHERE tag_domain_id = $1 AND aliased_id = $2",
					tag_domain_id.value(),
					aliased_id ) };

				if ( !existing.empty() && existing[ 0 ][ 0 ].as< TagID >() != alias_id )
					co_return createConflict(
						"Tag {} is already aliased to {} in domain {}",
						aliased_id,
						existing[ 0 ][ 0 ].as< TagID >(),
						tag_domain_id.value() );

				// already aliased to the requested target: idempotent success
			}
		}
		catch ( std::exception& e )
		{
			// the tag_aliases_before_insert trigger raises with this exact text (migration 187);
			// a rejected cycle is a conflict with existing relationships, not a server fault
			if ( std::string_view( e.what() ).find( "Cycle detected" ) != std::string_view::npos )
				co_return createConflict( "Error adding tag aliases: {}", e.what() );
			co_return createInternalError( "Failed to create alias: {}", e.what() );
		}
	}

	co_return drogon::HttpResponse::newHttpResponse();
}

} // namespace idhan::api
