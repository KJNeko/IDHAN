//
// Created by kj16609 on 5/3/25.
//

#include "api/TagAPI.hpp"

namespace idhan::api
{

drogon::Task< drogon::HttpResponsePtr > TagAPI::
	getTagRelationships( drogon::HttpRequestPtr request, const TagDomainID domain_id, const TagID tag_id )
{
	const auto db { drogon::app().getDbClient() };

	auto parents_result {
		db->execSqlCoro( "SELECT parent_id FROM tag_parents WHERE child_id = $1 AND domain_id = $2", tag_id, domain_id )
	};
	auto children_result {
		db->execSqlCoro( "SELECT child_id FROM tag_parents WHERE parent_id = $1 AND domain_id = $2", tag_id, domain_id )
	};

	auto older_siblings { db->execSqlCoro(
		"SELECT older_id FROM tag_siblings WHERE younger_id = $1 AND domain_id = $2", tag_id, domain_id ) };
	auto younger_siblings { db->execSqlCoro(
		"SELECT younger_id FROM tag_siblings WHERE older_id = $1 AND domain_id = $2", tag_id, domain_id ) };

	auto aliases_result { db->execSqlCoro(
		"SELECT alias_id FROM tag_aliases WHERE aliased_id = $1 AND domain_id = $2", tag_id, domain_id ) };
	auto aliased_result { db->execSqlCoro(
		"SELECT aliased_id FROM tag_aliases WHERE alias_id = $1 AND domain_id = $2", tag_id, domain_id ) };

	Json::Value json {};
	json[ "parents" ] = Json::arrayValue;
	json[ "children" ] = Json::arrayValue;
	json[ "older_siblings" ] = Json::arrayValue;
	json[ "younger_siblings" ] = Json::arrayValue;
	json[ "aliases" ] = Json::arrayValue;
	json[ "aliased" ] = Json::arrayValue;

	for ( const auto& row : co_await parents_result )
	{
		const auto parent_id { row[ 0 ].as< std::size_t >() };
		json[ "parents" ].append( parent_id );
	}

	for ( const auto& row : co_await children_result )
	{
		const auto child_id { row[ 0 ].as< std::size_t >() };
		json[ "children" ].append( child_id );
	}

	for ( const auto& row : co_await older_siblings )
	{
		const auto older_id { row[ 0 ].as< std::size_t >() };
		json[ "older_siblings" ].append( older_id );
	}

	for ( const auto& row : co_await younger_siblings )
	{
		const auto younger_id { row[ 0 ].as< std::size_t >() };
		json[ "younger_siblings" ].append( younger_id );
	}

	for ( const auto& row : co_await aliases_result )
	{
		const auto alias_id { row[ 0 ].as< std::size_t >() };
		json[ "aliases" ].append( alias_id );
	}

	for ( const auto& row : co_await aliased_result )
	{
		const auto aliased_id { row[ 0 ].as< std::size_t >() };
		json[ "aliased" ].append( aliased_id );
	}

	co_return drogon::HttpResponse::newHttpJsonResponse( json );
}

} // namespace idhan::api
