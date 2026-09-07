#include "api/FileRelationshipsAPI.hpp"
#include "api/helpers/helpers.hpp"

namespace idhan::api
{

constexpr std::uint16_t MAX_UNDECIDED_DISTANCE { 8 };
constexpr std::uint16_t DEFAULT_LIMIT { 1 };
constexpr std::uint16_t MAX_LIMIT { 32 };

drogon::Task< drogon::HttpResponsePtr > FileRelationshipsAPI::nextUndecided( const drogon::HttpRequestPtr request )
{
	const auto distance { helpers::parseBoundedParameter(
		request->getOptionalParameter< std::string >( "distance" ),
		"distance",
		MAX_UNDECIDED_DISTANCE,
		MAX_UNDECIDED_DISTANCE ) };
	if ( !distance ) co_return distance.error();

	const auto limit { helpers::parseBoundedParameter(
		request->getOptionalParameter< std::string >( "limit" ), "limit", DEFAULT_LIMIT, MAX_LIMIT ) };
	if ( !limit ) co_return limit.error();

	const auto db { drogon::app().getDbClient() };

	// Rulings delete distances. Group candidates by left_id for primary-key access, then order
	// each group by distance.
	const auto found { co_await db->execSqlCoro(
		"SELECT left_id, right_id, distance FROM hamming_distance "
		"WHERE distance <= $1::integer "
		"ORDER BY left_id, distance, right_id LIMIT $2::integer",
		static_cast< Integer >( *distance ),
		static_cast< Integer >( *limit ) ) };

	Json::Value json {};
	json[ "distance" ] = *distance;

	Json::Value pairs { Json::arrayValue };

	for ( const auto& row : found )
	{
		Json::Value pair {};
		pair[ "record_id_a" ] = row[ "left_id" ].as< RecordID >();
		pair[ "record_id_b" ] = row[ "right_id" ].as< RecordID >();
		pair[ "distance" ] = row[ "distance" ].as< Integer >();
		pairs.append( std::move( pair ) );
	}

	// Retain the single-pair response field for existing clients.
	json[ "pair" ] = pairs.empty() ? Json::nullValue : pairs[ 0 ];
	json[ "pairs" ] = std::move( pairs );

	co_return drogon::HttpResponse::newHttpJsonResponse( json );
}

} // namespace idhan::api
