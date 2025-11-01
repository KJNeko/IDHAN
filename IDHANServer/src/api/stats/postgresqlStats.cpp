//
// Created by kj16609 on 7/21/25.
//

#include "api/APIMaintenance.hpp"
#include "db/drogonArrayBind.hpp"

namespace idhan::api
{

drogon::Task< drogon::HttpResponsePtr > APIMaintenance::
	postgresqlStorageSunData( [[maybe_unused]] drogon::HttpRequestPtr request )
{
	Json::Value root {};
	root[ "name" ] = "root";
	root[ "children" ] = Json::Value( Json::arrayValue );

	auto db { drogon::app().getDbClient() };

	const auto table_list { co_await db->execSqlCoro(
		"SELECT table_name, pg_relation_size(quote_ident(table_name)::text) AS size FROM information_schema.tables WHERE "
		"table_schema = 'public' AND table_type = 'BASE TABLE'" ) };

	for ( const auto& table : table_list )
	{
		const auto table_name { table[ "table_name" ].as< std::string >() };
		const auto table_size { table[ "size" ].as< int64_t >() };

		Json::Value table_json {};
		table_json[ "name" ] = table_name;
		table_json[ "value" ] = table_size;
		table_json[ "children" ] = Json::Value( Json::arrayValue );

		const auto indicies { co_await db->execSqlCoro(
			"SELECT tablename AS table_name, indexname as index_name, pg_relation_size(indexname::regclass) AS index_size "
			"FROM pg_indexes WHERE tablename = $1 AND schemaname = 'public'",
			table_name ) };

		for ( const auto& index : indicies )
		{
			const auto index_name { index[ "index_name" ].as< std::string >() };
			const auto index_size { index[ "index_size" ].as< int64_t >() };

			Json::Value index_json {};
			index_json[ "name" ] = index_name;
			index_json[ "value" ] = index_size;

			table_json[ "children" ].append( index_json );
		}

		root[ "children" ].append( table_json );
	}

	co_return drogon::HttpResponse::newHttpJsonResponse( root );
}

drogon::Task< drogon::HttpResponsePtr > APIMaintenance::test( [[maybe_unused]] drogon::HttpRequestPtr request )
{
	co_return drogon::HttpResponse::newHttpJsonResponse( Json::Value() );
}

} // namespace idhan::api
