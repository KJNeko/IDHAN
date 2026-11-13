#include "helpers.hpp"

#include <algorithm>
#include <format>

namespace idhan::httpapi
{

runtime::Task< std::expected< void, http::Response > > validateRecordIds(
	std::vector< RecordID > record_ids,
	db::DBContext& db )
{
	for ( const auto record_id : record_ids )
		if ( record_id <= 0 )
			co_return std::unexpected( http::Response::error( 400, "Record IDs must be positive integers" ) );

	std::ranges::sort( record_ids );
	const auto duplicates { std::ranges::unique( record_ids ) };
	record_ids.erase( duplicates.begin(), duplicates.end() );

	db::DBBindings bindings {};
	bindings.bind( record_ids );

	const auto rows {
		co_await db.query( "SELECT record_id FROM records WHERE record_id = ANY($1)", std::move( bindings ) )
			.rows< RecordID >()
	};

	if ( rows.size() == record_ids.size() ) co_return {};

	std::vector< RecordID > found_ids {};
	found_ids.reserve( rows.size() );
	for ( const auto& [ record_id ] : rows ) found_ids.push_back( record_id );
	std::ranges::sort( found_ids );

	for ( const auto record_id : record_ids )
		if ( !std::ranges::binary_search( found_ids, record_id ) )
			co_return std::unexpected(
				http::Response::error( 404, std::format( "Record {} does not exist", record_id ) ) );

	co_return {};
}

} // namespace idhan::httpapi
