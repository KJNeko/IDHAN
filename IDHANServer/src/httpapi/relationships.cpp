#include "relationships.hpp"

#include <folly/io/IOBuf.h>

#include <algorithm>
#include <bitset>
#include <cstdlib>
#include <exception>
#include <format>
#include <libpq-fe.h>
#include <simdjson.h>
#include <unordered_set>

#include "apiAuth.hpp"
#include "db/similarRecords.hpp"
#include "helpers.hpp"
#include "idValidation.hpp"
#include "idhan/db/DBContext.hpp"
#include "idhan/http/routing/ReflectedRoutes.hpp"
#include "jsonBody.hpp"
#include "recordValidator.hpp"

namespace idhan::httpapi
{

http::HTTPTask< http::JsonResult< RecordRelationshipsResponse > > getRelationships(
	db::DBContext& db,
	http::RequestContext context )
{
	const auto record_id_e { context.parameters().scalar< RecordID >( "record_id" ) };
	if ( !record_id_e ) co_return http::Response::error( 400, record_id_e.error().message );
	const auto record_id { *record_id_e };

	db::DBBindings a {};
	a.bind( record_id );
	const auto inferior {
		co_await db
			.query(
				//language=postgresql
				R"SQL(
					SELECT
					    record_id
					FROM duplicate_members
					WHERE king_id = $1
					    AND record_id <> king_id
					ORDER BY
					    record_id
				)SQL",
				std::move( a ) )
			.rows< RecordID >()
	};

	db::DBBindings b {};
	b.bind( record_id );
	const auto superior {
		co_await db
			.query(
				//language=postgresql
				R"SQL(
					SELECT
					    king_id
					FROM duplicate_members
					WHERE record_id = $1
					    AND record_id <> king_id
				)SQL",
				std::move( b ) )
			.rows< RecordID >()
	};

	db::DBBindings c {};
	c.bind( record_id );
	const auto alternatives {
		co_await db
			.query(
				//language=postgresql
				R"SQL(
					SELECT
					    greater_record_id AS record_id
					FROM alternative_records
					WHERE lesser_record_id = duplicate_king ($1)
					UNION
					SELECT
					    lesser_record_id
					FROM alternative_records
					WHERE greater_record_id = duplicate_king ($1)
					ORDER BY
					    record_id
				)SQL",
				std::move( c ) )
			.rows< RecordID >()
	};

	std::vector< RecordID > inferior_ids {}, superior_ids {}, alternative_ids {};
	for ( const auto& [ id ] : inferior ) inferior_ids.push_back( id );
	for ( const auto& [ id ] : superior ) superior_ids.push_back( id );
	for ( const auto& [ id ] : alternatives ) alternative_ids.push_back( id );

	co_return http::JsonResponse< RecordRelationshipsResponse > {
		{ std::move( inferior_ids ), std::move( superior_ids ), std::move( alternative_ids ) }
	};
}

constexpr std::uint16_t default_similar_limit { 128 };

http::HTTPTask< http::JsonResult< SimilarRecordsResponse > > findSimilar(
	db::DBContext& db,
	http::RequestContext context,
	http::Query< FindSimilarQuery > query )
{
	const auto record_id_e { context.parameters().scalar< RecordID >( "record_id" ) };
	if ( !record_id_e ) co_return http::Response::error( 400, record_id_e.error().message );
	const auto record_id { *record_id_e };

	const auto distance { query.value.distance.value_or( 0 ) };
	const auto limit { query.value.limit.value_or( default_similar_limit ) };

	const bool include_unrelated { query.value.include_unrelated.value_or( false ) };
	const bool include_related { query.value.include_related.value_or( false ) };

	auto transaction { co_await db.transaction() };
	// Every lookup must see the same relationship and hash snapshot.
	co_await transaction.query( "SET TRANSACTION ISOLATION LEVEL REPEATABLE READ, READ ONLY" ).result();

	const auto probe { co_await db::similarityProbe( transaction, record_id ) };
	if ( !probe.has_value() )
	{
		co_await transaction.commit();
		co_return http::Response::error(
			404, std::format( "Record {} has no perceptual hash to compare against", record_id ) );
	}

	const auto& probe_value { probe.value() };
	std::unordered_set< RecordID > related_kings { probe_value.king_id };
	if ( !include_related )
	{
		const auto alternatives { co_await db::alternativeKingPairs( transaction, probe_value.king_id ) };
		for ( const auto& [ lesser, greater ] : alternatives )
			related_kings.insert( lesser == probe_value.king_id ? greater : lesser );
	}

	const auto unrelated_pairs { co_await db::unrelatedKingPairs( transaction, probe_value.king_id ) };

	std::unordered_set< RecordID > unrelated_kings;

	for ( const auto& [ lesser, greater ] : unrelated_pairs )
		unrelated_kings.insert( lesser == probe_value.king_id ? greater : lesser );

	const auto matches { co_await db::similarityCandidates(
		transaction, probe_value.phash, record_id, static_cast< Integer >( distance ) ) };

	co_await transaction.commit();

	SimilarRecordsResponse response { record_id, distance, limit, include_unrelated, include_related, false, {} };
	response.results.reserve( std::min< std::size_t >( matches.size(), limit ) );
	for ( const auto& [ match_id, match_distance, king_id ] : matches )
	{
		if ( !include_related && related_kings.contains( king_id ) ) continue;

		const bool unrelated { unrelated_kings.contains( king_id ) };
		if ( !include_unrelated && unrelated ) continue;

		if ( response.results.size() == limit )
		{
			response.truncated = true;
			break;
		}

		response.results.push_back( { match_id, match_distance, unrelated } );
	}
	co_return http::JsonResponse< SimilarRecordsResponse > { std::move( response ) };
}

constexpr std::uint16_t max_undecided_distance { 8 };
constexpr std::uint16_t default_undecided_limit { 1 };

http::HTTPTask< http::JsonResult< UndecidedDuplicatesResponse > > nextUndecided(
	db::DBContext& db,
	http::Query< NextUndecidedQuery > query )
{
	const auto distance { query.value.distance.value_or( max_undecided_distance ) };
	const auto limit { query.value.limit.value_or( default_undecided_limit ) };

	db::DBBindings bindings {};
	bindings.bind( static_cast< Integer >( distance ) );
	bindings.bind( static_cast< Integer >( limit ) );

	const auto found {
		co_await db
			.query(
				//language=postgresql
				R"SQL(
					SELECT
					    left_id,
					    right_id,
					    distance
					FROM hamming_distance
					WHERE distance <= $1::integer
					ORDER BY
					    left_id,
					    distance,
					    right_id
					LIMIT $2::integer
				)SQL",
				std::move( bindings ) )
			.rows< RecordID, RecordID, SmallInt >()
	};

	UndecidedDuplicatesResponse response { distance, {}, {} };
	response.pairs.reserve( found.size() );
	for ( const auto& [ left_id, right_id, pair_distance ] : found )
		response.pairs.push_back( { left_id, right_id, pair_distance } );

	if ( !response.pairs.empty() ) response.pair = response.pairs.front();

	co_return http::JsonResponse< UndecidedDuplicatesResponse > { std::move( response ) };
}

[[nodiscard]] static std::expected< std::pair< RecordID, RecordID >, http::Response > parsePairFromObject(
	const simdjson::dom::element object,
	const std::string_view a_key,
	const std::string_view b_key )
{
	if ( !object.is_object() )
		return std::unexpected(
			http::Response::error( 400, std::format( "Expected object with `{}` and `{}`", a_key, b_key ) ) );

	std::int64_t a {}, b {};
	if ( object[ a_key ].get( a ) != simdjson::SUCCESS || a < 0 )
		return std::unexpected(
			http::Response::error(
				400, std::format( "Expected `{}` and `{}` to be unsigned integers", a_key, b_key ) ) );
	if ( object[ b_key ].get( b ) != simdjson::SUCCESS || b < 0 )
		return std::unexpected(
			http::Response::error(
				400, std::format( "Expected `{}` and `{}` to be unsigned integers", a_key, b_key ) ) );

	auto a_id_e { positiveId< RecordID >( a, a_key ) };
	if ( !a_id_e ) return std::unexpected( std::move( a_id_e.error() ) );

	auto b_id_e { positiveId< RecordID >( b, b_key ) };
	if ( !b_id_e ) return std::unexpected( std::move( b_id_e.error() ) );

	const auto a_id { *a_id_e };
	const auto b_id { *b_id_e };
	if ( a_id == b_id )
		return std::unexpected(
			http::Response::error( 400, std::format( "`{}` and `{}` cannot be the same record", a_key, b_key ) ) );

	return std::pair { a_id, b_id };
}

http::HTTPTask< http::JsonResult< RelationshipUpdatedResponse > > setBetterDuplicate(
	db::DBContext& db,
	http::RequestContext context )
{
	auto body_e { readValidatedJsonBody( context ) };
	if ( !body_e ) co_return std::move( body_e.error() );

	simdjson::dom::parser parser;
	simdjson::dom::element root {};
	try
	{
		root = parser.parse( body_e->data(), body_e->size() );
	}
	catch ( const simdjson::simdjson_error& e )
	{
		co_return http::Response::error( 400, e.what() );
	}

	std::vector< std::pair< RecordID, RecordID > > pairs {};

	if ( root.is_array() )
	{
		simdjson::dom::array array {};
		if ( root.get_array().get( array ) != simdjson::SUCCESS )
			co_return http::Response::error( 500, "Unexpected: array conversion failed after is_array() check" );
		for ( const simdjson::dom::element object : array )
		{
			auto pair { parsePairFromObject( object, "worse_id", "better_id" ) };
			if ( !pair ) co_return std::move( pair.error() );
			pairs.push_back( *pair );
		}
	}
	else if ( root.is_object() )
	{
		auto pair { parsePairFromObject( root, "worse_id", "better_id" ) };
		if ( !pair ) co_return std::move( pair.error() );
		pairs.push_back( *pair );
	}
	else
		co_return http::Response::error(
			400,
			"Expected json body of either array of objects, or a single object. Objects must have `better_id` and `worse_id`" );

	std::vector< RecordID > referenced {};
	referenced.reserve( pairs.size() * 2 );
	for ( const auto& [ worse_id, better_id ] : pairs )
	{
		referenced.push_back( worse_id );
		referenced.push_back( better_id );
	}

	auto validation { co_await validateRecordIds( std::move( referenced ), db ) };
	if ( !validation ) co_return std::move( validation.error() );

	if ( pairs.empty() ) co_return http::JsonResponse< RelationshipUpdatedResponse > {};

	std::vector< RecordID > worse_ids {};
	std::vector< RecordID > better_ids {};
	worse_ids.reserve( pairs.size() );
	better_ids.reserve( pairs.size() );
	for ( const auto& [ worse_id, better_id ] : pairs )
	{
		worse_ids.push_back( worse_id );
		better_ids.push_back( better_id );
	}

	try
	{
		db::DBBindings bindings {};
		bindings.bind( std::move( worse_ids ) );
		bindings.bind( std::move( better_ids ) );
		co_await db
			.query(
				//language=postgresql
				R"SQL(
					SELECT
					    insert_duplicate_pair ($1::INTEGER[], $2::INTEGER[])
				)SQL",
				std::move( bindings ) )
			.result();
	}
	catch ( const std::exception& e )
	{
		co_return http::Response::error( 500, std::format( "Failed to set duplicate relationships: {}", e.what() ) );
	}

	co_return http::JsonResponse< RelationshipUpdatedResponse > {};
}

http::HTTPTask< http::JsonResult< RelationshipUpdatedResponse > > addAlternative(
	db::DBContext& db,
	http::RequestContext context )
{
	auto body_e { readValidatedJsonBody( context ) };
	if ( !body_e ) co_return std::move( body_e.error() );

	simdjson::dom::parser parser;
	simdjson::dom::array array {};
	try
	{
		if ( parser.parse( body_e->data(), body_e->size() ).get( array ) != simdjson::SUCCESS )
			co_return http::Response::error( 400, "Expected json array of integers" );
	}
	catch ( const simdjson::simdjson_error& e )
	{
		co_return http::Response::error( 400, e.what() );
	}

	if ( array.size() < 2 )
		co_return http::Response::error( 400, "Expected at least 2 record ids to pair as alternatives" );

	std::vector< RecordID > record_ids {};
	for ( const simdjson::dom::element item : array )
	{
		auto id { parsePositiveId< RecordID >( item, "record_id" ) };
		if ( !id ) co_return std::move( id.error() );

		record_ids.push_back( *id );
	}

	std::ranges::sort( record_ids );
	const auto repeated { std::ranges::unique( record_ids ) };
	record_ids.erase( repeated.begin(), repeated.end() );

	if ( record_ids.size() < 2 ) co_return http::Response::error( 400, "Expected at least 2 distinct record ids" );

	auto validation { co_await validateRecordIds( record_ids, db ) };
	if ( !validation ) co_return std::move( validation.error() );

	std::vector< RecordID > lesser {}, greater {};
	for ( std::size_t i = 0; i < record_ids.size(); ++i )
		for ( std::size_t j = i + 1; j < record_ids.size(); ++j )
		{
			lesser.push_back( record_ids[ i ] );
			greater.push_back( record_ids[ j ] );
		}

	db::DBBindings bindings {};
	bindings.bind( lesser );
	bindings.bind( greater );

	co_await db
		.query(
			//language=postgresql
			R"SQL(
				INSERT INTO alternative_records (lesser_record_id, greater_record_id)
				SELECT
				    *
				FROM UNNEST($1, $2)
				ON CONFLICT
				    DO NOTHING
			)SQL",
			std::move( bindings ) )
		.result();

	co_return http::JsonResponse< RelationshipUpdatedResponse > {};
}

http::HTTPTask< http::JsonResult< ClearedRelationshipResponse > > clearRelationship(
	db::DBContext& db,
	http::RequestContext context )
{
	auto body_e { readValidatedJsonBody( context ) };
	if ( !body_e ) co_return std::move( body_e.error() );

	simdjson::dom::parser parser;
	simdjson::dom::element root {};
	try
	{
		root = parser.parse( body_e->data(), body_e->size() );
	}
	catch ( const simdjson::simdjson_error& e )
	{
		co_return http::Response::error( 400, e.what() );
	}

	auto pair { parsePairFromObject( root, "record_id_a", "record_id_b" ) };
	if ( !pair ) co_return std::move( pair.error() );

	const auto [ record_id_a, record_id_b ] { *pair };

	auto validation { co_await validateRecordIds( { record_id_a, record_id_b }, db ) };
	if ( !validation ) co_return std::move( validation.error() );

	auto transaction { co_await db.transaction() };
	co_await transaction
		.query(
			//language=postgresql
			R"SQL(
				SELECT
				    pg_advisory_xact_lock(16609, 1)
			)SQL" )
		.result();

	db::DBBindings duplicate_bindings {};
	duplicate_bindings.bind( record_id_a );
	duplicate_bindings.bind( record_id_b );
	const auto removed_rows {
		co_await transaction
			.query(
				//language=postgresql
				R"SQL(
					SELECT
					    remove_duplicate_pair ($1, $2) AS removed
				)SQL",
				std::move( duplicate_bindings ) )
			.rows< bool >()
	};
	const bool duplicate_removed { !removed_rows.empty() && std::get< 0 >( removed_rows.row( 0 ) ) };

	db::DBBindings unrelated_bindings {};
	unrelated_bindings.bind( record_id_a );
	unrelated_bindings.bind( record_id_b );
	const auto unrelated_result {
		co_await transaction
			.query(
				//language=postgresql
				R"SQL(
					DELETE FROM unrelated_records
					WHERE lesser_record_id = LEAST (duplicate_king ($1), duplicate_king ($2))
					    AND greater_record_id = GREATEST (duplicate_king ($1), duplicate_king ($2))
				)SQL",
				std::move( unrelated_bindings ) )
			.result()
	};

	db::DBBindings alternative_bindings {};
	alternative_bindings.bind( record_id_a );
	alternative_bindings.bind( record_id_b );
	const auto alternative_result {
		co_await transaction
			.query(
				//language=postgresql
				R"SQL(
					DELETE FROM alternative_records
					WHERE lesser_record_id = LEAST (duplicate_king ($1), duplicate_king ($2))
					    AND greater_record_id = GREATEST (duplicate_king ($1), duplicate_king ($2))
				)SQL",
				std::move( alternative_bindings ) )
			.result()
	};

	const bool alternative_removed { std::atoll( PQcmdTuples( alternative_result.get() ) ) > 0 };
	const bool unrelated_removed { std::atoll( PQcmdTuples( unrelated_result.get() ) ) > 0 };

	co_await transaction.commit();

	co_return http::JsonResponse< ClearedRelationshipResponse > {
		{ duplicate_removed, alternative_removed, unrelated_removed }
	};
}

http::HTTPTask< http::JsonResult< RelationshipUpdatedResponse > > setUnrelated(
	db::DBContext& db,
	http::RequestContext context )
{
	auto body_e { readValidatedJsonBody( context ) };
	if ( !body_e ) co_return std::move( body_e.error() );

	simdjson::dom::parser parser;
	simdjson::dom::element root {};
	try
	{
		root = parser.parse( body_e->data(), body_e->size() );
	}
	catch ( const simdjson::simdjson_error& e )
	{
		co_return http::Response::error( 400, e.what() );
	}

	auto pair { parsePairFromObject( root, "record_id_a", "record_id_b" ) };
	if ( !pair ) co_return std::move( pair.error() );

	const auto [ lesser, greater ] { std::minmax( pair->first, pair->second ) };

	auto validation { co_await validateRecordIds( { lesser, greater }, db ) };
	if ( !validation ) co_return std::move( validation.error() );

	db::DBBindings bindings {};
	bindings.bind( lesser );
	bindings.bind( greater );

	co_await db
		.query(
			//language=postgresql
			R"SQL(
				INSERT INTO unrelated_records (lesser_record_id, greater_record_id)
				    VALUES ($1, $2)
				ON CONFLICT
				    DO NOTHING
			)SQL",
			std::move( bindings ) )
		.result();

	co_return http::JsonResponse< RelationshipUpdatedResponse > {};
}

} // namespace idhan::httpapi
