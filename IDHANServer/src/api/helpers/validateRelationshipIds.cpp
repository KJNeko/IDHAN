#include <algorithm>

#include "api/helpers/createBadRequest.hpp"
#include "api/helpers/helpers.hpp"
#include "db/drogonArrayBind.hpp"
#include "drogon/HttpAppFramework.h"

namespace idhan::api::helpers
{

ExpectedTask< void > validateRelationshipIds(
	const TagDomainID tag_domain_id,
	std::vector< TagID > tag_ids,
	DbClientPtr db )
{
	const auto domain_result {
		co_await db->execSqlCoro( "SELECT 1 FROM tag_domains WHERE tag_domain_id = $1", tag_domain_id )
	};

	if ( domain_result.empty() )
		co_return std::unexpected( createNotFound( "Tag domain {} does not exist", tag_domain_id ) );

	std::ranges::sort( tag_ids );
	const auto duplicates { std::ranges::unique( tag_ids ) };
	tag_ids.erase( duplicates.begin(), duplicates.end() );

	const auto tags_result { co_await db->execSqlCoro(
		"SELECT tag_id FROM tags WHERE tag_id = ANY($1::INTEGER[])",
		std::forward< const std::vector< TagID > >( tag_ids ) ) };

	if ( tags_result.size() == tag_ids.size() ) co_return {};

	std::vector< TagID > found_ids {};
	found_ids.reserve( tags_result.size() );
	for ( const auto& row : tags_result ) found_ids.emplace_back( row[ 0 ].as< TagID >() );
	std::ranges::sort( found_ids );

	for ( const auto tag_id : tag_ids )
		if ( !std::ranges::binary_search( found_ids, tag_id ) )
			co_return std::unexpected( createNotFound( "Tag {} does not exist", tag_id ) );

	co_return {};
}

} // namespace idhan::api::helpers
