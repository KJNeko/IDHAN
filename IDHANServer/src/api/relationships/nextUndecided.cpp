#include "api/FileRelationshipsAPI.hpp"
#include "api/helpers/helpers.hpp"

namespace idhan::api
{

constexpr std::uint16_t MAX_UNDECIDED_DISTANCE { 8 };

drogon::Task< drogon::HttpResponsePtr > FileRelationshipsAPI::nextUndecided( const drogon::HttpRequestPtr request )
{
	const auto distance { helpers::parseBoundedParameter(
		request->getOptionalParameter< std::string >( "distance" ),
		"distance",
		MAX_UNDECIDED_DISTANCE,
		MAX_UNDECIDED_DISTANCE ) };
	if ( !distance ) co_return distance.error();

	const bool include_unrelated { request->getOptionalParameter< bool >( "include_unrelated" ).value_or( false ) };

	const auto db { drogon::app().getDbClient() };

	// undecided_hamming_distance and unrelated_records both store the lower id first, so the pair
	// matches column for column
	const auto pair { co_await db->execSqlCoro(
		"SELECT left_id, right_id, distance FROM undecided_hamming_distance undecided "
		"WHERE distance <= $1::integer "
		"AND ($2::boolean OR NOT EXISTS ("
		"  SELECT 1 FROM unrelated_records unrelated "
		"  WHERE unrelated.lesser_record_id = undecided.left_id "
		"    AND unrelated.greater_record_id = undecided.right_id)) "
		"ORDER BY distance, left_id, right_id LIMIT 1",
		static_cast< Integer >( *distance ),
		include_unrelated ) };

	Json::Value json {};
	json[ "distance" ] = *distance;
	json[ "include_unrelated" ] = include_unrelated;

	if ( pair.empty() )
	{
		json[ "pair" ] = Json::nullValue;
		co_return drogon::HttpResponse::newHttpJsonResponse( json );
	}

	Json::Value found {};
	found[ "record_id_a" ] = pair[ 0 ][ "left_id" ].as< RecordID >();
	found[ "record_id_b" ] = pair[ 0 ][ "right_id" ].as< RecordID >();
	found[ "distance" ] = pair[ 0 ][ "distance" ].as< Integer >();
	json[ "pair" ] = std::move( found );

	co_return drogon::HttpResponse::newHttpJsonResponse( json );
}

} // namespace idhan::api
