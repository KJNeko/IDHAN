#include "api/FileRelationshipsAPI.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "api/helpers/helpers.hpp"

namespace idhan::api
{

constexpr std::uint16_t MAX_DISTANCE { 64 };
constexpr std::uint16_t DEFAULT_LIMIT { 128 };
constexpr std::uint16_t MAX_LIMIT { 1024 };

drogon::Task< drogon::HttpResponsePtr > FileRelationshipsAPI::findSimilar(
	const drogon::HttpRequestPtr request,
	const RecordID record_id )
{
	const auto distance { helpers::parseBoundedParameter(
		request->getOptionalParameter< std::string >( "distance" ), "distance", 0, MAX_DISTANCE ) };
	if ( !distance ) co_return distance.error();

	const auto limit { helpers::parseBoundedParameter(
		request->getOptionalParameter< std::string >( "limit" ), "limit", DEFAULT_LIMIT, MAX_LIMIT ) };
	if ( !limit ) co_return limit.error();

	const bool include_unrelated { request->getOptionalParameter< bool >( "include_unrelated" ).value_or( false ) };
	const bool include_related { request->getOptionalParameter< bool >( "include_related" ).value_or( false ) };

	const auto db { drogon::app().getDbClient() };

	const auto validation { co_await helpers::validateRecordIds( { record_id }, db ) };
	if ( !validation ) co_return validation.error();

	const auto probe_result { co_await db->execSqlCoro(
		"SELECT phash FROM image_metadata WHERE record_id = $1 AND phash IS NOT NULL", record_id ) };

	if ( probe_result.empty() )
		co_return createNotFound( "Record {} has no perceptual hash to compare against", record_id );

	// the bit column arrives as its text representation, and goes back the same way
	const auto probe { probe_result[ 0 ][ 0 ].as< std::string >() };

	// Resolve the probe's flat duplicate group once, not per candidate row.
	const auto matches { co_await db->execSqlCoro(
		"WITH duplicate_group(record_id) AS ("
		"  SELECT member.record_id FROM flattened_duplicates probe"
		"  JOIN flattened_duplicates member USING (root_id)"
		"  WHERE probe.record_id = $2 AND member.record_id != $2"
		"), related(record_id) AS ("
		"  SELECT record_id FROM duplicate_group"
		"  UNION SELECT greater_record_id FROM alternative_records WHERE lesser_record_id = $2"
		"  UNION SELECT lesser_record_id FROM alternative_records WHERE greater_record_id = $2"
		") "
		"SELECT record_id, bit_count(phash # $1)::integer AS distance, "
		"EXISTS ("
		"  SELECT 1 FROM unrelated_records marked "
		"  WHERE (marked.lesser_record_id = $2 AND marked.greater_record_id = image_metadata.record_id) "
		"     OR (marked.greater_record_id = $2 AND marked.lesser_record_id = image_metadata.record_id)) AS unrelated "
		"FROM image_metadata "
		"WHERE phash IS NOT NULL AND record_id != $2 AND bit_count(phash # $1) <= $3::integer "
		"AND ($5::boolean OR NOT EXISTS ("
		"  SELECT 1 FROM unrelated_records unrelated "
		"  WHERE (unrelated.lesser_record_id = $2 AND unrelated.greater_record_id = image_metadata.record_id) "
		"     OR (unrelated.greater_record_id = $2 AND unrelated.lesser_record_id = image_metadata.record_id))) "
		"AND ($6::boolean OR NOT EXISTS ("
		"  SELECT 1 FROM related WHERE related.record_id = image_metadata.record_id)) "
		"ORDER BY distance, record_id LIMIT $4::integer",
		probe,
		record_id,
		static_cast< Integer >( *distance ),
		*limit + 1,
		include_unrelated,
		include_related ) };

	const bool truncated { matches.size() > *limit };
	Json::Value results { Json::arrayValue };
	for ( std::size_t index = 0; index < std::min< std::size_t >( matches.size(), *limit ); ++index )
	{
		const auto& row { matches[ index ] };
		Json::Value match {};
		match[ "record_id" ] = row[ "record_id" ].as< RecordID >();
		match[ "distance" ] = row[ "distance" ].as< Integer >();
		match[ "unrelated" ] = row[ "unrelated" ].as< bool >();
		results.append( std::move( match ) );
	}

	Json::Value json {};
	json[ "record_id" ] = record_id;
	json[ "distance" ] = *distance;
	json[ "limit" ] = *limit;
	json[ "include_unrelated" ] = include_unrelated;
	json[ "include_related" ] = include_related;
	json[ "truncated" ] = truncated;
	json[ "results" ] = std::move( results );

	co_return drogon::HttpResponse::newHttpJsonResponse( json );
}

} // namespace idhan::api
