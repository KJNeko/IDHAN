//
// Created by kj16609 on 11/18/24.
//

#include "IDHANTypes.hpp"
#include "api/IDHANTagAPI.hpp"
#include "drogon/HttpResponse.h"

namespace idhan::api
{

//TODO: Move this out of here into generic handlers
drogon::HttpResponsePtr generateFailedTagSearch( const TagID tag_id )
{
	Json::Value value;
	value[ "code" ] = 404;
	value[ "message" ] = std::format(
		"TagID {} was not found. Either you tried to request it before it was comitted, or it did not exist", tag_id );

	auto response { drogon::HttpResponse::newHttpJsonResponse( value ) };

	return response;
}

drogon::Task< drogon::HttpResponsePtr > IDHANTagAPI::info( drogon::HttpRequestPtr request, TagID tag_id )
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
			co_return generateFailedTagSearch( tag_id );
		}

		namespace_id = result[ 0 ][ 0 ].as< NamespaceID >();
		subtag_id = result[ 0 ][ 1 ].as< SubtagID >();
	}

	{
		//TODO: Figure out how many files use this tag
		root[ "items_count" ] = 0;
	}

	// find namespace info
	{
		const auto result { co_await db->execSqlCoro(
			"SELECT namespace_text, color FROM tag_namespaces WHERE namespace_id = $1", namespace_id ) };

		if ( result.size() == 0 )
		{
			throw std::runtime_error( "IDHANApi::info: no namespace found" );
		}

		root[ "namespace" ][ "id" ] = namespace_id;
		root[ "namespace" ][ "text" ] = result[ 0 ][ 0 ].as< std::string >();

		if ( !result[ 0 ][ 1 ].isNull() )
		{
			const auto& color { result[ 0 ][ 1 ].as< std::vector< char > >() };

			//TODO: hex
			root[ "color" ][ "hex" ] = "FIXME";
			root[ "color" ][ "r" ] = static_cast< int >( color[ 0 ] );
			root[ "color" ][ "g" ] = static_cast< int >( color[ 1 ] );
			root[ "color" ][ "b" ] = static_cast< int >( color[ 2 ] );
		}
	}

	{
		const auto result {
			co_await db->execSqlCoro( "SELECT subtag_id FROM tag_subtags WHERE subtag_id = $1", subtag_id )
		};

		if ( result.size() == 0 )
		{
			throw std::runtime_error( "IDHANApi::info: no subtag found" );
		}

		root[ "subtag" ][ "id" ] = subtag_id;
		root[ "subtag" ][ "text" ] = result[ 0 ][ 0 ].as< std::string >();
	}
}

} // namespace idhan::api
