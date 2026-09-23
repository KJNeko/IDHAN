#include "relationships.hpp"

#include <folly/io/IOBuf.h>

#include <algorithm>
#include <bitset>
#include <cstdlib>
#include <exception>
#include <format>
#include <libpq-fe.h>
#include <simdjson.h>

#include "apiAuth.hpp"
#include "helpers.hpp"
#include "idhan/db/DBContext.hpp"
#include "jsonBody.hpp"
#include "recordValidator.hpp"

namespace idhan::httpapi
{

static void appendRecordIdArray(
	simdjson::builder::string_builder& builder,
	const std::string_view key,
	const std::vector< RecordID >& ids )
{
	builder.escape_and_append_with_quotes( key );
	builder.append_colon();
	builder.start_array();
	bool first { true };
	for ( const auto id : ids )
	{
		if ( !first ) builder.append_comma();
		builder.append( id );
		first = false;
	}
	builder.end_array();
}

[[nodiscard]] static http::Response jsonResponse(
	simdjson::builder::string_builder& builder,
	const std::uint16_t status = 200 )
{
	auto json { builder.view() };
	if ( json.error() ) throw std::runtime_error( simdjson::error_message( json.error() ) );

	return http::Response::json( status, folly::IOBuf::copyBuffer( *json ) );
}

static http::HTTPTask< http::Response > getRelationships( db::DBContext& db, http::RequestContext context )
{
	const auto record_id_e { context.parameters().scalar< RecordID >( "record_id" ) };
	if ( !record_id_e ) co_return http::Response::error( 400, record_id_e.error().message );
	const auto record_id { *record_id_e };

	db::DBBindings a {};
	a.bind( record_id );
	const auto inferior {
		co_await db
			.query(
				"SELECT worse_record_id FROM duplicate_pairs WHERE better_record_id = $1 "
				"ORDER BY worse_record_id",
				std::move( a ) )
			.rows< RecordID >()
	};

	db::DBBindings b {};
	b.bind( record_id );
	const auto superior {
		co_await db.query( "SELECT better_record_id FROM duplicate_pairs WHERE worse_record_id = $1", std::move( b ) )
			.rows< RecordID >()
	};

	db::DBBindings c {};
	c.bind( record_id );
	const auto alternatives {
		co_await db
			.query(
				"SELECT greater_record_id AS record_id FROM alternative_records"
				"  WHERE lesser_record_id = duplicate_king($1)"
				"  UNION"
				"  SELECT lesser_record_id FROM alternative_records WHERE greater_record_id = duplicate_king($1)"
				" ORDER BY record_id",
				std::move( c ) )
			.rows< RecordID >()
	};

	std::vector< RecordID > inferior_ids {}, superior_ids {}, alternative_ids {};
	for ( const auto& [ id ] : inferior ) inferior_ids.push_back( id );
	for ( const auto& [ id ] : superior ) superior_ids.push_back( id );
	for ( const auto& [ id ] : alternatives ) alternative_ids.push_back( id );

	simdjson::builder::string_builder builder;
	builder.start_object();
	appendRecordIdArray( builder, "inferior", inferior_ids );
	builder.append_comma();
	appendRecordIdArray( builder, "superior", superior_ids );
	builder.append_comma();
	appendRecordIdArray( builder, "alternatives", alternative_ids );
	builder.end_object();

	co_return jsonResponse( builder );
}

constexpr std::uint16_t max_distance { 64 };
constexpr std::uint16_t default_similar_limit { 128 };
constexpr std::uint16_t max_similar_limit { 1024 };

static http::HTTPTask< http::Response > findSimilar( db::DBContext& db, http::RequestContext context )
{
	const auto record_id_e { context.parameters().scalar< RecordID >( "record_id" ) };
	if ( !record_id_e ) co_return http::Response::error( 400, record_id_e.error().message );
	const auto record_id { *record_id_e };

	const auto distance_param_e { context.parameters().optional< std::string >( "distance" ) };
	if ( !distance_param_e ) co_return http::Response::error( 400, distance_param_e.error().message );
	auto distance_e { parseBoundedParameter( *distance_param_e, "distance", 0, max_distance ) };
	if ( !distance_e ) co_return std::move( distance_e.error() );

	const auto limit_param_e { context.parameters().optional< std::string >( "limit" ) };
	if ( !limit_param_e ) co_return http::Response::error( 400, limit_param_e.error().message );
	auto limit_e { parseBoundedParameter( *limit_param_e, "limit", default_similar_limit, max_similar_limit ) };
	if ( !limit_e ) co_return std::move( limit_e.error() );

	const auto distance { *distance_e };
	const auto limit { *limit_e };

	const auto include_unrelated_param { context.parameters().optional< std::string >( "include_unrelated" ) };
	if ( !include_unrelated_param ) co_return http::Response::error( 400, include_unrelated_param.error().message );
	const bool include_unrelated {
		include_unrelated_param->value_or( "false" ) == "true" || include_unrelated_param->value_or( "false" ) == "1"
	};

	const auto include_related_param { context.parameters().optional< std::string >( "include_related" ) };
	if ( !include_related_param ) co_return http::Response::error( 400, include_related_param.error().message );
	const bool include_related {
		include_related_param->value_or( "false" ) == "true" || include_related_param->value_or( "false" ) == "1"
	};

	db::DBBindings probe_bindings {};
	probe_bindings.bind( record_id );
	const auto probe_rows {
		co_await db
			.query(
				"SELECT phash FROM image_metadata WHERE record_id = $1 AND phash IS NOT NULL",
				std::move( probe_bindings ) )
			.rows< std::bitset< 64 > >()
	};

	if ( probe_rows.empty() )
		co_return http::Response::error(
			404, std::format( "Record {} has no perceptual hash to compare against", record_id ) );

	const auto probe { std::get< 0 >( probe_rows.row( 0 ) ) };

	db::DBBindings bindings {};
	bindings.bind( probe );
	bindings.bind( record_id );
	bindings.bind( static_cast< std::int32_t >( distance ) );
	bindings.bind( static_cast< std::int32_t >( limit ) + 1 );
	bindings.bind( include_unrelated );
	bindings.bind( include_related );

	const auto matches {
		co_await db
			.query(
				"WITH probe_root AS (SELECT duplicate_king($2) AS root_id), related(record_id) AS ("
				"  SELECT duplicate_group_members(root_id) FROM probe_root"
				"  UNION SELECT member.record_id FROM probe_root"
				"  JOIN alternative_records alternative"
				"    ON alternative.lesser_record_id = root_id OR alternative.greater_record_id = root_id"
				"  CROSS JOIN LATERAL duplicate_group_members("
				"    CASE WHEN alternative.lesser_record_id = root_id THEN alternative.greater_record_id"
				"         ELSE alternative.lesser_record_id END) AS member(record_id)"
				") "
				"SELECT record_id, (phash <~> $1)::integer AS distance, "
				"EXISTS ("
				"  SELECT 1 FROM unrelated_records marked "
				"  WHERE marked.lesser_record_id = LEAST(duplicate_king($2), duplicate_king(image_metadata.record_id)) "
				"    AND marked.greater_record_id = GREATEST(duplicate_king($2), duplicate_king(image_metadata.record_id))) AS unrelated "
				"FROM image_metadata "
				"WHERE phash IS NOT NULL AND record_id != $2 AND (phash <~> $1)::integer <= $3::integer "
				"AND ($5::boolean OR NOT EXISTS ("
				"  SELECT 1 FROM unrelated_records unrelated "
				"  WHERE unrelated.lesser_record_id = LEAST(duplicate_king($2), duplicate_king(image_metadata.record_id)) "
				"    AND unrelated.greater_record_id = GREATEST(duplicate_king($2), duplicate_king(image_metadata.record_id)))) "
				"AND ($6::boolean OR NOT EXISTS ("
				"  SELECT 1 FROM related WHERE related.record_id = image_metadata.record_id)) "
				"ORDER BY distance, record_id LIMIT $4::integer",
				std::move( bindings ) )
			.rows< RecordID, std::int32_t, bool >()
	};

	const bool truncated { matches.size() > limit };

	simdjson::builder::string_builder builder;
	builder.start_object();
	builder.append_key_value( "record_id", record_id );
	builder.append_comma();
	builder.append_key_value( "distance", distance );
	builder.append_comma();
	builder.append_key_value( "limit", limit );
	builder.append_comma();
	builder.append_key_value( "include_unrelated", include_unrelated );
	builder.append_comma();
	builder.append_key_value( "include_related", include_related );
	builder.append_comma();
	builder.append_key_value( "truncated", truncated );
	builder.append_comma();
	builder.escape_and_append_with_quotes( "results" );
	builder.append_colon();
	builder.start_array();
	const auto result_count { std::min< std::size_t >( matches.size(), limit ) };
	for ( std::size_t index = 0; index < result_count; ++index )
	{
		if ( index != 0 ) builder.append_comma();
		const auto& [ match_id, match_distance, unrelated ] { matches.row( index ) };
		builder.start_object();
		builder.append_key_value( "record_id", match_id );
		builder.append_comma();
		builder.append_key_value( "distance", match_distance );
		builder.append_comma();
		builder.append_key_value( "unrelated", unrelated );
		builder.end_object();
	}
	builder.end_array();
	builder.end_object();

	co_return jsonResponse( builder );
}

constexpr std::uint16_t max_undecided_distance { 8 };
constexpr std::uint16_t default_undecided_limit { 1 };
constexpr std::uint16_t max_undecided_limit { 32 };

static http::HTTPTask< http::Response > nextUndecided( db::DBContext& db, http::RequestContext context )
{
	const auto distance_param_e { context.parameters().optional< std::string >( "distance" ) };
	if ( !distance_param_e ) co_return http::Response::error( 400, distance_param_e.error().message );
	auto distance_e {
		parseBoundedParameter( *distance_param_e, "distance", max_undecided_distance, max_undecided_distance )
	};
	if ( !distance_e ) co_return std::move( distance_e.error() );

	const auto limit_param_e { context.parameters().optional< std::string >( "limit" ) };
	if ( !limit_param_e ) co_return http::Response::error( 400, limit_param_e.error().message );
	auto limit_e { parseBoundedParameter( *limit_param_e, "limit", default_undecided_limit, max_undecided_limit ) };
	if ( !limit_e ) co_return std::move( limit_e.error() );

	const auto distance { *distance_e };
	const auto limit { *limit_e };

	db::DBBindings bindings {};
	bindings.bind( static_cast< std::int32_t >( distance ) );
	bindings.bind( static_cast< std::int32_t >( limit ) );

	const auto found {
		co_await db
			.query(
				"SELECT left_id, right_id, distance FROM hamming_distance "
				"WHERE distance <= $1::integer "
				"ORDER BY left_id, distance, right_id LIMIT $2::integer",
				std::move( bindings ) )
			.rows< RecordID, RecordID, std::int32_t >()
	};

	simdjson::builder::string_builder builder;
	builder.start_object();
	builder.append_key_value( "distance", distance );
	builder.append_comma();
	builder.escape_and_append_with_quotes( "pairs" );
	builder.append_colon();
	builder.start_array();
	bool first { true };
	for ( const auto& [ left_id, right_id, pair_distance ] : found )
	{
		if ( !first ) builder.append_comma();
		first = false;
		builder.start_object();
		builder.append_key_value( "record_id_a", left_id );
		builder.append_comma();
		builder.append_key_value( "record_id_b", right_id );
		builder.append_comma();
		builder.append_key_value( "distance", pair_distance );
		builder.end_object();
	}
	builder.end_array();
	builder.append_comma();
	builder.escape_and_append_with_quotes( "pair" );
	builder.append_colon();
	if ( found.empty() )
		builder.append_raw( "null" );
	else
	{
		const auto& [ left_id, right_id, pair_distance ] { found.row( 0 ) };
		builder.start_object();
		builder.append_key_value( "record_id_a", left_id );
		builder.append_comma();
		builder.append_key_value( "record_id_b", right_id );
		builder.append_comma();
		builder.append_key_value( "distance", pair_distance );
		builder.end_object();
	}
	builder.end_object();

	co_return jsonResponse( builder );
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

	const auto a_id { static_cast< RecordID >( a ) };
	const auto b_id { static_cast< RecordID >( b ) };
	if ( a_id == b_id )
		return std::unexpected(
			http::Response::error( 400, std::format( "`{}` and `{}` cannot be the same record", a_key, b_key ) ) );

	return std::pair { a_id, b_id };
}

static http::HTTPTask< http::Response > setBetterDuplicate( db::DBContext& db, http::RequestContext context )
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

	for ( const auto& [ worse_id, better_id ] : pairs )
	{
		try
		{
			db::DBBindings bindings {};
			bindings.bind( worse_id );
			bindings.bind( better_id );
			co_await db.query( "SELECT insert_duplicate_pair($1, $2)", std::move( bindings ) ).result();
		}
		catch ( const std::exception& e )
		{
			co_return http::Response::error(
				500,
				std::format(
					"Failed to set record {} as a worse duplicate of {}: {}", worse_id, better_id, e.what() ) );
		}
	}

	co_return http::Response::json( 200, folly::IOBuf::copyBuffer( std::string_view { "{}" } ) );
}

static http::HTTPTask< http::Response > addAlternative( db::DBContext& db, http::RequestContext context )
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
		std::int64_t id {};
		if ( item.get_int64().get( id ) != simdjson::SUCCESS || id < 0 )
			co_return http::Response::error( 400, "Expected json array of unsigned integers" );
		record_ids.push_back( static_cast< RecordID >( id ) );
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
			"INSERT INTO alternative_records (lesser_record_id, greater_record_id) "
			"SELECT * FROM UNNEST($1, $2) "
			"ON CONFLICT DO NOTHING",
			std::move( bindings ) )
		.result();

	co_return http::Response::json( 200, folly::IOBuf::copyBuffer( std::string_view { "{}" } ) );
}

static http::HTTPTask< http::Response > clearRelationship( db::DBContext& db, http::RequestContext context )
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
	co_await transaction.query( "SELECT pg_advisory_xact_lock(16609, 1)" ).result();

	db::DBBindings duplicate_bindings {};
	duplicate_bindings.bind( record_id_a );
	duplicate_bindings.bind( record_id_b );
	const auto removed_rows {
		co_await transaction.query( "SELECT remove_duplicate_pair($1, $2) AS removed", std::move( duplicate_bindings ) )
			.rows< bool >()
	};
	const bool duplicate_removed { !removed_rows.empty() && std::get< 0 >( removed_rows.row( 0 ) ) };

	db::DBBindings unrelated_bindings {};
	unrelated_bindings.bind( record_id_a );
	unrelated_bindings.bind( record_id_b );
	const auto unrelated_result {
		co_await transaction
			.query(
				"DELETE FROM unrelated_records "
				"WHERE lesser_record_id = LEAST(duplicate_king($1), duplicate_king($2)) "
				"AND greater_record_id = GREATEST(duplicate_king($1), duplicate_king($2))",
				std::move( unrelated_bindings ) )
			.result()
	};

	db::DBBindings alternative_bindings {};
	alternative_bindings.bind( record_id_a );
	alternative_bindings.bind( record_id_b );
	const auto alternative_result {
		co_await transaction
			.query(
				"DELETE FROM alternative_records "
				"WHERE lesser_record_id = LEAST(duplicate_king($1), duplicate_king($2)) "
				"AND greater_record_id = GREATEST(duplicate_king($1), duplicate_king($2))",
				std::move( alternative_bindings ) )
			.result()
	};

	const bool alternative_removed { std::atoll( PQcmdTuples( alternative_result.get() ) ) > 0 };
	const bool unrelated_removed { std::atoll( PQcmdTuples( unrelated_result.get() ) ) > 0 };

	co_await transaction.commit();

	simdjson::builder::string_builder builder;
	builder.start_object();
	builder.append_key_value( "duplicate_removed", duplicate_removed );
	builder.append_comma();
	builder.append_key_value( "alternative_removed", alternative_removed );
	builder.append_comma();
	builder.append_key_value( "unrelated_removed", unrelated_removed );
	builder.end_object();

	co_return jsonResponse( builder );
}

static http::HTTPTask< http::Response > setUnrelated( db::DBContext& db, http::RequestContext context )
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
			"INSERT INTO unrelated_records (lesser_record_id, greater_record_id) VALUES ($1, $2) "
			"ON CONFLICT DO NOTHING",
			std::move( bindings ) )
		.result();

	co_return http::Response::json( 200, folly::IOBuf::copyBuffer( std::string_view { "{}" } ) );
}

std::vector< http::RouteDefinition > relationshipRoutes( db::DBContext& db )
{
	std::vector< http::RouteDefinition > routes {};

	const auto add =
		[ &routes, &db ](
			const proxygen::HTTPMethod method,
			std::string path,
			std::vector< http::PathParameter > path_parameters,
			std::vector< http::RequestValidation > before_body,
			http::HTTPTask< http::Response > ( *handler )( db::DBContext&, http::RequestContext ) )
	{
		before_body.insert( before_body.begin(), apiAuthValidation( db ) );

		auto bound = [ &db, handler ]( http::RequestContext context ) -> http::HTTPTask< http::Response >
		{ co_return co_await handler( db, std::move( context ) ); };

		routes.push_back(
			http::RouteDefinition {
				http::RouteMetadata {
					method, std::move( path ), std::move( path_parameters ), std::move( before_body ) },
				std::move( bound ) } );
	};

	add( proxygen::HTTPMethod::GET,
	     "/relationships/{record_id}",
	     { recordIdPathParameter() },
	     { recordExistsValidation( db ) },
	     getRelationships );
	add( proxygen::HTTPMethod::GET,
	     "/relationships/{record_id}/similar",
	     { recordIdPathParameter() },
	     { recordExistsValidation( db ) },
	     findSimilar );
	add( proxygen::HTTPMethod::GET, "/relationships/duplicates/undecided", {}, {}, nextUndecided );
	add( proxygen::HTTPMethod::POST, "/relationships/duplicates/add", {}, {}, setBetterDuplicate );
	add( proxygen::HTTPMethod::POST, "/relationships/alternatives/add", {}, {}, addAlternative );
	add( proxygen::HTTPMethod::POST, "/relationships/clear", {}, {}, clearRelationship );
	add( proxygen::HTTPMethod::POST, "/relationships/unrelated/add", {}, {}, setUnrelated );

	return routes;
}

} // namespace idhan::httpapi
