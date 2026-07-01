//
// Created by kj16609 on 7/21/25.
//

#include "api/APIMaintenance.hpp"
#include "db/drogonArrayBind.hpp"

namespace idhan::api
{

drogon::Task< drogon::HttpResponsePtr > APIMaintenance::postgresqlStorageSunData(
	[[maybe_unused]] drogon::HttpRequestPtr request )
{
	const auto db { drogon::app().getDbClient() };

	const auto rows { co_await db->execSqlCoro(
		"SELECT t.table_name, "
		"       pg_relation_size(quote_ident(t.table_name)::text) AS table_size, "
		"       i.indexname AS index_name, "
		"       pg_relation_size(i.indexname::regclass) AS index_size "
		"FROM information_schema.tables t "
		"LEFT JOIN pg_indexes i "
		"       ON i.tablename = t.table_name AND i.schemaname = current_schema() "
		"WHERE t.table_schema = current_schema() AND t.table_type = 'BASE TABLE' "
		"ORDER BY t.table_name" ) };

	Json::Value root {};
	root[ "name" ] = "root";
	root[ "children" ] = Json::Value( Json::arrayValue );

	std::string current_table {};
	Json::Value current_table_json {};

	const auto flush { [ & ]() { if ( !current_table.empty() ) root[ "children" ].append( current_table_json ); } };

	for ( const auto& row : rows )
	{
		const auto table_name { row[ "table_name" ].as< std::string >() };

		if ( table_name != current_table )
		{
			flush();
			current_table = table_name;
			current_table_json = Json::Value {};
			current_table_json[ "name" ] = table_name;
			current_table_json[ "value" ] = row[ "table_size" ].as< int64_t >();
			current_table_json[ "children" ] = Json::Value( Json::arrayValue );
		}

		if ( !row[ "index_name" ].isNull() )
		{
			Json::Value index_json {};
			index_json[ "name" ] = row[ "index_name" ].as< std::string >();
			index_json[ "value" ] = row[ "index_size" ].as< int64_t >();
			current_table_json[ "children" ].append( index_json );
		}
	}
	flush();

	co_return drogon::HttpResponse::newHttpJsonResponse( root );
}

drogon::Task< drogon::HttpResponsePtr > APIMaintenance::test( [[maybe_unused]] drogon::HttpRequestPtr request )
{
	co_return drogon::HttpResponse::newHttpJsonResponse( Json::Value() );
}

} // namespace idhan::api
