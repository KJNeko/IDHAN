//
// Created by kj16609 on 11/18/24.
//

#include "IDHANTypes.hpp"
#include "api/TagAPI.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "drogon/HttpResponse.h"
#include "logging/format_ns.hpp"

namespace idhan::api
{

drogon::Task< drogon::HttpResponsePtr > TagAPI::
	getTagInfo( [[maybe_unused]] const drogon::HttpRequestPtr request, const TagID tag_id )
{
	Json::Value root {};
	root[ "tag_id" ] = tag_id;

	NamespaceID namespace_id {};
	SubtagID subtag_id {};

	auto db { drogon::app().getDbClient() };

	{
		const auto result {
			co_await db->execSqlCoro( "SELECT namespace_id, subtag_id FROM tags WHERE tag_id = $1", tag_id )
		};

		if ( result.empty() )
		{
			co_return internal::createBadResponse(
				"TagID {} was not found. Either you tried to request it before it was committed, or it does not exist",
				tag_id );
		}

		namespace_id = result[ 0 ][ 0 ].as< NamespaceID >();
		subtag_id = result[ 0 ][ 1 ].as< SubtagID >();
	}

	{
		//TODO: Figure out how many files use this tag
		const auto count_result {
			co_await db
				->execSqlCoro( "SELECT storage_count, display_count FROM total_tag_counts WHERE tag_id = $1", tag_id )
		};

		if ( !count_result.empty() )
			root[ "items_count" ] = count_result[ 0 ][ 0 ].as< std::size_t >();
		else
			root[ "items_count" ] = 0;
	}

	// find namespace info
	{
		const auto result { co_await db->execSqlCoro(
			"SELECT namespace_text, color FROM tag_namespaces WHERE namespace_id = $1", namespace_id ) };

		if ( result.empty() )
		{
			co_return createInternalError(
				"No namespace found for tag {}. Expected namespace ID {}", tag_id, namespace_id );
		}

		root[ "namespace" ][ "id" ] = namespace_id;
		root[ "namespace" ][ "text" ] = result[ 0 ][ 0 ].as< std::string >();

		if ( !result[ 0 ][ 1 ].isNull() )
		{
			const auto& color { result[ 0 ][ 1 ].as< std::vector< char > >() };

			std::string hex_str {};
			for ( const auto& c : color )
			{
				hex_str += format_ns::format( "%02x", static_cast< int >( c ) );
			}
			root[ "color" ][ "hex" ] = hex_str;
			root[ "color" ][ "r" ] = static_cast< int >( color[ 0 ] );
			root[ "color" ][ "g" ] = static_cast< int >( color[ 1 ] );
			root[ "color" ][ "b" ] = static_cast< int >( color[ 2 ] );
		}
	}

	{
		const auto result {
			co_await db->execSqlCoro( "SELECT subtag_text FROM tag_subtags WHERE subtag_id = $1", subtag_id )
		};

		if ( result.empty() )
		{
			co_return createInternalError( "No subtag found for tag {}, Expected subtag ID {}", tag_id, subtag_id );
		}

		root[ "subtag" ][ "id" ] = subtag_id;
		root[ "subtag" ][ "text" ] = result[ 0 ][ 0 ].as< std::string >();
	}

	co_return drogon::HttpResponse::newHttpJsonResponse( root );
}

} // namespace idhan::api
