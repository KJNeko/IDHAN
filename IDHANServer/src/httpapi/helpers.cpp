#include "helpers.hpp"

#include <algorithm>
#include <charconv>
#include <format>

namespace idhan::httpapi
{

runtime::Task< std::expected< void, http::Response > > validateRecordIds(
	std::vector< RecordID > record_ids,
	db::DBContext& db )
{
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

std::expected< std::uint16_t, http::Response > parseBoundedParameter(
	const std::optional< std::string >& parameter,
	const std::string_view name,
	const std::uint16_t fallback,
	const std::uint16_t maximum )
{
	if ( !parameter ) return fallback;

	std::uint16_t value { 0 };
	const auto& text { *parameter };
	const auto [ end, error ] { std::from_chars( text.data(), text.data() + text.size(), value ) };

	if ( error != std::errc {} || end != text.data() + text.size() )
		return std::unexpected(
			http::Response::error( 400, std::format( "`{}` must be an unsigned integer, got `{}`", name, text ) ) );

	if ( value > maximum )
		return std::unexpected(
			http::Response::error(
				400, std::format( "`{}` must be no greater than {}, got {}", name, maximum, value ) ) );

	return value;
}

} // namespace idhan::httpapi
