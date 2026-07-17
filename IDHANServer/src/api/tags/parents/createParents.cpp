//
// Created by kj16609 on 3/11/25.
//

#include <expected>

#include "api/TagAPI.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "api/helpers/helpers.hpp"
#include "logging/log.hpp"

namespace idhan::api
{

drogon::Task< drogon::HttpResponsePtr > TagAPI::createTagParents( const drogon::HttpRequestPtr request )
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

	std::vector< std::pair< TagID, TagID > > pairs {};
	pairs.reserve( json.size() );
	std::vector< TagID > referenced_tags {};
	referenced_tags.reserve( static_cast< std::size_t >( json.size() ) * 2 );

	for ( const auto& item : json )
	{
		const auto& parent { item[ "parent_id" ] };
		const auto& child { item[ "child_id" ] };

		if ( !parent.isIntegral() ) co_return createBadRequest( "Invalid parent item: Must be in TagID form" );
		if ( !child.isIntegral() ) co_return createBadRequest( "Invalid child item: Must be in TagID form" );

		const TagID parent_id { parent.as< TagID >() };
		const TagID child_id { child.as< TagID >() };

		// the check_parent_cycle trigger only walks existing rows, so a direct
		// self-parent would pass it and create self-referential parent mappings
		if ( parent_id == child_id )
			co_return createBadRequest( "Cannot parent a tag to itself {} == {}", parent_id, child_id );

		pairs.emplace_back( parent_id, child_id );
		referenced_tags.emplace_back( parent_id );
		referenced_tags.emplace_back( child_id );
	}

	// unknown IDs would otherwise surface as FK-violation 500s
	const auto validation {
		co_await helpers::validateRelationshipIds( tag_domain_id.value(), std::move( referenced_tags ), db )
	};
	if ( !validation ) co_return validation.error();

	for ( const auto& [ parent_id, child_id ] : pairs )
	{
		try
		{
			co_await db->execSqlCoro(
				"INSERT INTO tag_parents (tag_domain_id, parent_id, child_id) VALUES ($1, $2, $3) "
				"ON CONFLICT(tag_domain_id, parent_id, child_id) DO NOTHING",
				tag_domain_id.value(),
				parent_id,
				child_id );
		}
		catch ( std::exception& e )
		{
			// the check_parent_cycle trigger raises with this exact text (migration 105);
			// a rejected cycle is a conflict with existing relationships, not a server fault
			if ( std::string_view( e.what() ).find( "Cycle detected" ) != std::string_view::npos )
				co_return createConflict( "Error adding tag parents: {}", e.what() );
			co_return createInternalError( "Error adding tag parents: {}", e.what() );
		}
	}

	co_return drogon::HttpResponse::newHttpResponse();
}

} // namespace idhan::api
