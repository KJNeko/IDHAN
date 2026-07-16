//
// Created by kj16609 on 11/11/25.
//

#include "IDHANTypes.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "db/dbTypes.hpp"
#include "db/drogonArrayBind.hpp"
#include "drogon/utils/coroutine.h"
#include "threading/ExpectedTask.hpp"

namespace idhan
{

ExpectedTask< std::unordered_map< std::string, TagID > > mapTags(
	const std::vector< std::string >& tags,
	DbClientPtr db )
{
	constexpr auto query { "SELECT tag_id, tag_text FROM tags WHERE tag_text = ANY($1::TEXT[])" };

	const auto tag_id_result {
		co_await db->execSqlCoro( query, std::forward< const std::vector< std::string > >( tags ) )
	};

	// no size pre-check here: an unknown tag must reach the loop below for a proper 404,
	// and duplicated input tags legitimately return fewer rows than tags.size()
	std::unordered_map< std::string, TagID > tag_ids_result {};

	for ( const auto& row : tag_id_result )
	{
		tag_ids_result.emplace( row[ "tag_text" ].as< std::string >(), row[ "tag_id" ].as< TagID >() );
	}

	for ( const auto& tag : tags )
		if ( !tag_ids_result.contains( tag ) ) [[unlikely]]
			co_return std::unexpected( createNotFound( "Was unable to get ID for tag {}, Tag does not exist", tag ) );

	co_return tag_ids_result;
}

} // namespace idhan
