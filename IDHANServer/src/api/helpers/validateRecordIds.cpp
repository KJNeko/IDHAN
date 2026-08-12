#include <algorithm>

#include "api/helpers/createBadRequest.hpp"
#include "api/helpers/helpers.hpp"
#include "caching/LRUCache.hpp"
#include "db/drogonArrayBind.hpp"
#include "drogon/HttpAppFramework.h"
#include "fgl/literals.hpp"

namespace idhan::api::helpers
{

ExpectedTask< void > validateRecordIds( std::vector< RecordID > record_ids, DbClientPtr db )
{
	std::ranges::sort( record_ids );
	const auto duplicates { std::ranges::unique( record_ids ) };
	record_ids.erase( duplicates.begin(), duplicates.end() );

	using namespace fgl::literals;

	const auto records_result { co_await db->execSqlCoro(
		"SELECT record_id FROM records WHERE record_id = ANY($1::INTEGER[])",
		std::forward< const std::vector< RecordID > >( record_ids ) ) };

	if ( records_result.size() == record_ids.size() ) co_return {};

	std::vector< RecordID > found_ids {};
	found_ids.reserve( records_result.size() );
	for ( const auto& row : records_result ) found_ids.emplace_back( row[ 0 ].as< RecordID >() );
	std::ranges::sort( found_ids );

	for ( const auto record_id : record_ids )
		if ( !std::ranges::binary_search( found_ids, record_id ) )
			co_return std::unexpected( createNotFound( "Record {} does not exist", record_id ) );

	co_return {};
}

} // namespace idhan::api::helpers
