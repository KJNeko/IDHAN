#include "tags.hpp"

#include <folly/io/IOBuf.h>

#include <algorithm>
#include <format>
#include <simdjson.h>

#include "apiAuth.hpp"
#include "db/tagDomainCounts.hpp"
#include "idhan/db/DBContext.hpp"
#include "jsonBody.hpp"
#include "idValidation.hpp"
#include "logging/format_ns.hpp"
#include "logging/log.hpp"
#include "tagHelpers.hpp"

namespace idhan::httpapi
{

static http::HTTPTask< std::expected< std::vector< TagDomainID >, http::Response > > tagCountDomains(
	db::DBContext& db,
	const http::RequestParameters& parameters,
	bool accept_singular = false );

static http::HTTPTask< http::Response > getTagInfo(
	db::DBContext& db,
	const TagID tag_id,
	std::vector< TagDomainID > domains )
{
	if ( tag_id <= 0 ) co_return http::Response::error( 400, "tag_id must be positive" );

	db::DBBindings bindings {};
	bindings.bind( tag_id );

	const auto rows {
		co_await db
			.query(
				"SELECT t.namespace_id, tn.namespace_text, t.subtag_text "
				"FROM tags t "
				"JOIN tag_namespaces tn ON tn.namespace_id = t.namespace_id "
				"WHERE t.tag_id = $1",
				std::move( bindings ) )
			.rows< NamespaceID, std::string, std::string >()
	};

	if ( rows.empty() )
		co_return http::Response::error(
			400,
			std::format(
				"TagID {} was not found. Either you tried to request it before it was committed, or it does not exist",
				tag_id ) );

	const auto& [ namespace_id, namespace_text, subtag_text ] { rows.row( 0 ) };

	const auto counts { co_await db::tagDomainCounts( db, tag_id, std::move( domains ) ) };
	std::int64_t storage_count {};
	std::int64_t display_count {};
	for ( const auto& count : counts )
	{
		storage_count = std::max( storage_count, count.storage_count );
		display_count = std::max( display_count, count.display_count );
	}

	simdjson::builder::string_builder builder;
	builder.start_object();
	builder.append_key_value( "tag_id", tag_id );
	builder.append_comma();
	builder.escape_and_append_with_quotes( "namespace" );
	builder.append_colon();
	builder.start_object();
	builder.append_key_value( "id", namespace_id );
	builder.append_comma();
	builder.append_key_value( "text", namespace_text );
	builder.end_object();
	builder.append_comma();
	builder.escape_and_append_with_quotes( "subtag" );
	builder.append_colon();
	builder.start_object();
	builder.append_key_value( "text", subtag_text );
	builder.end_object();
	builder.append_comma();
	builder.append_key_value( "storage_count", storage_count );
	builder.append_comma();
	builder.append_key_value( "display_count", display_count );
	builder.append_comma();
	builder.escape_and_append_with_quotes( "domain_counts" );
	builder.append_colon();
	builder.start_array();
	bool first { true };
	for ( const auto& count : counts )
	{
		if ( !first ) builder.append_comma();
		first = false;
		builder.start_object();
		builder.append_key_value( "tag_domain_id", count.domain_id );
		builder.append_comma();
		builder.append_key_value( "storage_count", count.storage_count );
		builder.append_comma();
		builder.append_key_value( "display_count", count.display_count );
		builder.end_object();
	}
	builder.end_array();
	builder.end_object();

	auto json { builder.view() };
	if ( json.error() ) throw std::runtime_error( simdjson::error_message( json.error() ) );

	co_return http::Response::json( 200, folly::IOBuf::copyBuffer( *json ) );
}

static http::HTTPTask< http::Response > getTagInfoFromPath( db::DBContext& db, http::RequestContext context )
{
	const auto tag_id_e { context.parameters().scalar< TagID >( "tag_id" ) };
	if ( !tag_id_e ) co_return http::Response::error( 400, tag_id_e.error().message );

	auto domains { co_await tagCountDomains( db, context.parameters() ) };
	if ( !domains ) co_return std::move( domains.error() );

	co_return co_await getTagInfo( db, *tag_id_e, std::move( *domains ) );
}

static http::HTTPTask< http::Response > getTagInfoFromQuery( db::DBContext& db, http::RequestContext context )
{
	auto tag_id_e { context.parameters().optional< TagID >( "tag_id" ) };
	if ( !tag_id_e ) co_return http::Response::error( 400, tag_id_e.error().message );

	if ( !*tag_id_e )
	{
		auto tag_ids_e { context.parameters().optional< TagID >( "tag_ids" ) };
		if ( !tag_ids_e ) co_return http::Response::error( 400, tag_ids_e.error().message );
		tag_id_e = *tag_ids_e;
	}

	if ( !*tag_id_e ) co_return http::Response::error( 400, "tag_id or tag_ids query parameter is required" );

	auto domains { co_await tagCountDomains( db, context.parameters() ) };
	if ( !domains ) co_return std::move( domains.error() );

	co_return co_await getTagInfo( db, **tag_id_e, std::move( *domains ) );
}

static http::HTTPTask< std::vector< TagID > > relationshipQuery(
	db::DBContext& db,
	std::string sql,
	const TagID tag_id,
	const TagDomainID tag_domain_id )
{
	db::DBBindings bindings {};
	bindings.bind( tag_id );
	bindings.bind( tag_domain_id );

	const auto rows { co_await db.query( std::move( sql ), std::move( bindings ) ).rows< TagID >() };

	std::vector< TagID > ids {};
	ids.reserve( rows.size() );
	for ( const auto& [ id ] : rows ) ids.push_back( id );

	co_return ids;
}

static void appendTagIdArray(
	simdjson::builder::string_builder& builder,
	const std::string_view key,
	const std::vector< TagID >& ids )
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

static http::HTTPTask< http::Response > getTagRelationships( db::DBContext& db, http::RequestContext context )
{
	const auto domain_id_e { context.parameters().scalar< TagDomainID >( "domain_id" ) };
	if ( !domain_id_e ) co_return http::Response::error( 400, domain_id_e.error().message );
	const auto tag_id_e { context.parameters().scalar< TagID >( "tag_id" ) };
	if ( !tag_id_e ) co_return http::Response::error( 400, tag_id_e.error().message );

	const auto tag_id { *tag_id_e };
	const auto tag_domain_id { *domain_id_e };

	const auto parents { co_await relationshipQuery(
		db, "SELECT parent_id FROM tag_parents WHERE child_id = $1 AND tag_domain_id = $2", tag_id, tag_domain_id ) };
	const auto children { co_await relationshipQuery(
		db, "SELECT child_id FROM tag_parents WHERE parent_id = $1 AND tag_domain_id = $2", tag_id, tag_domain_id ) };
	const auto older_siblings { co_await relationshipQuery(
		db, "SELECT older_id FROM tag_siblings WHERE younger_id = $1 AND tag_domain_id = $2", tag_id, tag_domain_id ) };
	const auto younger_siblings { co_await relationshipQuery(
		db, "SELECT younger_id FROM tag_siblings WHERE older_id = $1 AND tag_domain_id = $2", tag_id, tag_domain_id ) };
	const auto aliases { co_await relationshipQuery(
		db, "SELECT alias_id FROM tag_aliases WHERE aliased_id = $1 AND tag_domain_id = $2", tag_id, tag_domain_id ) };
	const auto aliased { co_await relationshipQuery(
		db, "SELECT aliased_id FROM tag_aliases WHERE alias_id = $1 AND tag_domain_id = $2", tag_id, tag_domain_id ) };

	simdjson::builder::string_builder builder;
	builder.start_object();
	appendTagIdArray( builder, "parents", parents );
	builder.append_comma();
	appendTagIdArray( builder, "children", children );
	builder.append_comma();
	appendTagIdArray( builder, "older_siblings", older_siblings );
	builder.append_comma();
	appendTagIdArray( builder, "younger_siblings", younger_siblings );
	builder.append_comma();
	appendTagIdArray( builder, "aliases", aliases );
	builder.append_comma();
	appendTagIdArray( builder, "aliased", aliased );
	builder.end_object();

	auto json { builder.view() };
	if ( json.error() ) throw std::runtime_error( simdjson::error_message( json.error() ) );

	co_return http::Response::json( 200, folly::IOBuf::copyBuffer( *json ) );
}

static http::HTTPTask< http::Response > search( db::DBContext& db, http::RequestContext context )
{
	const auto tag_text_e { context.parameters().scalar< std::string >( "tag" ) };
	if ( !tag_text_e ) co_return http::Response::error( 400, tag_text_e.error().message );

	db::DBBindings bindings {};
	bindings.bind( *tag_text_e );

	const auto rows {
		co_await db
			.query( "SELECT tag_id FROM tags WHERE tag_text = CASEFOLD(normalize($1, NFC))", std::move( bindings ) )
			.rows< TagID >()
	};

	simdjson::builder::string_builder builder;
	builder.start_object();
	if ( !rows.empty() )
	{
		builder.append_key_value( "tag_id", std::get< 0 >( rows.row( 0 ) ) );
		builder.append_comma();
		builder.append_key_value( "found", true );
	}
	else
		builder.append_key_value( "found", false );
	builder.end_object();

	auto json { builder.view() };
	if ( json.error() ) throw std::runtime_error( simdjson::error_message( json.error() ) );

	co_return http::Response::json( 200, folly::IOBuf::copyBuffer( *json ) );
}

http::HTTPTask< std::vector< SimilarTag > > getSimilarTags(
	db::DBContext& db,
	std::string search_value,
	const std::size_t limit,
	const bool include_unused,
	const HydrusDisplayType display,
	std::vector< TagDomainID > domains )
{
	constexpr std::size_t max_limit { 256 };

	const bool is_negative { search_value.starts_with( '-' ) };
	const std::string real_search_value { is_negative ? search_value.substr( 1 ) : search_value };
	const auto wrapped_search_value { format_ns::format( "%{}%", real_search_value ) };

	constexpr std::string_view query { R"(
		WITH matches AS (
			SELECT tags.tag_text, tags.tag_id,
				COALESCE(MAX(CASE WHEN $5 THEN tc.storage_count ELSE tc.display_count END), 0)::BIGINT AS use_count
			FROM tags
			LEFT JOIN tag_counts tc ON tc.tag_id = tags.tag_id
				AND (cardinality($4::SMALLINT[]) = 0 OR tc.tag_domain_id = ANY($4::SMALLINT[]))
			WHERE tag_text LIKE CASEFOLD(normalize($1, NFC))
			GROUP BY tags.tag_id
		)
		SELECT tag_text, tag_id,
			similarity(tag_text, CASEFOLD(normalize($2, NFC)))::FLOAT8 AS similarity,
			use_count
		FROM matches
		WHERE $6 OR use_count > 0
		ORDER BY (tag_text = CASEFOLD(normalize($2, NFC))) DESC,
			(similarity(tag_text, CASEFOLD(normalize($2, NFC))) * use_count) DESC,
			similarity DESC, tag_id ASC
		LIMIT $3
	)" };

	db::DBBindings bindings {};
	bindings.bind( wrapped_search_value );
	bindings.bind( real_search_value );
	bindings.bind( static_cast< std::int64_t >( std::min( limit, max_limit ) ) );
	bindings.bind( domains );
	bindings.bind( display == HydrusDisplayType::STORED );
	bindings.bind( include_unused );

	const auto rows { co_await db.query( std::string { query }, std::move( bindings ) )
		                  .rows< std::string, TagID, double, std::int64_t >() };

	std::vector< SimilarTag > out {};
	out.reserve( rows.size() );
	for ( const auto& [ tag_text, tag_id, similarity, count ] : rows )
		out.push_back( SimilarTag { tag_text, similarity, tag_id, count } );

	co_return out;
}

static http::HTTPTask< std::expected< std::vector< TagDomainID >, http::Response > > tagCountDomains(
	db::DBContext& db,
	const http::RequestParameters& parameters,
	const bool accept_singular )
{
	auto domains { parameters.vector< TagDomainID >( "tag_domains" ) };
	if ( !domains ) co_return std::unexpected( http::Response::error( 400, domains.error().message ) );

	if ( accept_singular )
	{
		if ( domains->empty() )
		{
			auto legacy_domain { parameters.optional< TagDomainID >( "tag_domain" ) };
			if ( !legacy_domain )
				co_return std::unexpected( http::Response::error( 400, legacy_domain.error().message ) );

			if ( *legacy_domain ) domains->push_back( **legacy_domain );
		}
	}

	std::ranges::sort( *domains );
	const auto duplicates { std::ranges::unique( *domains ) };
	domains->erase( duplicates.begin(), duplicates.end() );

	for ( const auto domain_id : *domains )
	{
		if ( domain_id <= 0 )
			co_return std::unexpected( http::Response::error( 400, "tag_domains must contain positive IDs" ) );

		const auto domain { co_await findTagDomain( domain_id, db ) };
		if ( !domain )
			co_return std::unexpected(
				http::Response::error( 404, std::format( "Tag domain {} does not exist", domain_id ) ) );
	}

	co_return std::move( *domains );
}

static http::HTTPTask< http::Response > autocomplete( db::DBContext& db, http::RequestContext context )
{
	const auto tag_text_e { context.parameters().scalar< std::string >( "tag" ) };
	if ( !tag_text_e ) co_return http::Response::error( 400, tag_text_e.error().message );

	const auto display_type_e { context.parameters().optional< std::string >( "tag_display_type" ) };
	if ( !display_type_e ) co_return http::Response::error( 400, display_type_e.error().message );

	const auto display_type { display_type_e->value_or( "display" ) };
	if ( display_type != "storage" && display_type != "display" )
		co_return http::Response::error( 400, "Invalid tag display type" );

	const auto limit_e { context.parameters().optional< std::size_t >( "limit" ) };
	if ( !limit_e ) co_return http::Response::error( 400, limit_e.error().message );

	const auto include_unused_e { context.parameters().optional< bool >( "include_unused" ) };
	if ( !include_unused_e ) co_return http::Response::error( 400, include_unused_e.error().message );

	const bool include_unused { include_unused_e->value_or( true ) };

	auto domains { co_await tagCountDomains( db, context.parameters(), true ) };
	if ( !domains ) co_return std::move( domains.error() );

	const auto display { display_type == "storage" ? HydrusDisplayType::STORED : HydrusDisplayType::DISPLAY };
	const auto results { co_await getSimilarTags(
		db, *tag_text_e, limit_e->value_or( 10 ), include_unused, display, std::move( *domains ) ) };

	simdjson::builder::string_builder builder;
	builder.start_array();
	bool first { true };
	for ( const auto& tag : results )
	{
		if ( !first ) builder.append_comma();
		first = false;

		builder.start_object();
		builder.append_key_value( "tag_text", tag.tag_text );
		builder.append_comma();
		builder.append_key_value( "similarity", tag.similarity );
		builder.append_comma();
		builder.append_key_value( "tag_id", tag.tag_id );
		builder.append_comma();
		builder.append_key_value( "count", tag.count );
		builder.end_object();
	}
	builder.end_array();

	auto json { builder.view() };
	if ( json.error() ) throw std::runtime_error( simdjson::error_message( json.error() ) );

	co_return http::Response::json( 200, folly::IOBuf::copyBuffer( *json ) );
}

static http::HTTPTask< http::Response > createTagsFromRequest( db::DBContext& db, http::RequestContext context )
{
	auto body_e { readValidatedJsonBody( context ) };
	if ( !body_e ) co_return std::move( body_e.error() );

	simdjson::dom::parser parser;
	simdjson::dom::array items {};
	try
	{
		if ( parser.parse( body_e->data(), body_e->size() ).get( items ) != simdjson::SUCCESS )
			co_return http::Response::error( 400, "Invalid json object. Expected array as root item" );
	}
	catch ( const simdjson::simdjson_error& e )
	{
		co_return http::Response::error( 400, e.what() );
	}

	std::vector< std::pair< std::string, std::string > > tag_pairs {};
	tag_pairs.reserve( items.size() );
	for ( const simdjson::dom::element item : items )
	{
		std::string_view tag_namespace {}, subtag {};
		if ( item[ "namespace" ].get( tag_namespace ) != simdjson::SUCCESS )
			co_return http::Response::error( 400, "Invalid namespace: Expected string" );
		if ( item[ "subtag" ].get( subtag ) != simdjson::SUCCESS )
			co_return http::Response::error( 400, "Invalid subtag: Expected string" );

		tag_pairs.emplace_back( tag_namespace, subtag );
	}

	auto tag_ids { co_await createTagsFromPairs( std::move( tag_pairs ), db ) };
	if ( !tag_ids ) co_return std::move( tag_ids.error() );

	simdjson::builder::string_builder builder;
	builder.start_array();
	bool first { true };
	for ( const auto tag_id : *tag_ids )
	{
		if ( !first ) builder.append_comma();
		first = false;
		builder.start_object();
		builder.append_key_value( "tag_id", tag_id );
		builder.end_object();
	}
	builder.end_array();

	auto json { builder.view() };
	if ( json.error() ) throw std::runtime_error( simdjson::error_message( json.error() ) );

	co_return http::Response::json( 200, folly::IOBuf::copyBuffer( *json ) );
}

static void appendDomainJson( simdjson::builder::string_builder& builder, const TagDomainInfo& domain )
{
	builder.start_object();
	builder.append_key_value( "tag_domain_id", domain.id );
	builder.append_comma();
	builder.append_key_value( "domain_name", domain.name );
	builder.end_object();
}

static http::HTTPTask< http::Response > createTagDomain( db::DBContext& db, http::RequestContext context )
{
	auto body_e { readValidatedJsonBody( context ) };
	if ( !body_e ) co_return std::move( body_e.error() );

	simdjson::dom::parser parser;
	std::string_view name {};
	try
	{
		const simdjson::dom::element root { parser.parse( body_e->data(), body_e->size() ) };
		if ( !root.is_object() )
			co_return http::Response::error( 400, "Invalid json object. Expected object as root item" );
		if ( root[ "name" ].get( name ) != simdjson::SUCCESS )
			co_return http::Response::error( 400, "No valid json input" );
	}
	catch ( const simdjson::simdjson_error& e )
	{
		co_return http::Response::error( 400, e.what() );
	}

	if ( const auto existing { co_await findTagDomain( name, db ) }; existing )
	{
		log::debug( "Found existing tag domain with name '{}'", name );

		simdjson::builder::string_builder builder;
		appendDomainJson( builder, *existing );
		auto json { builder.view() };
		if ( json.error() ) throw std::runtime_error( simdjson::error_message( json.error() ) );

		co_return http::Response::json( 409, folly::IOBuf::copyBuffer( *json ) );
	}

	db::DBBindings bindings {};
	bindings.bind( std::string { name } );

	const auto rows {
		co_await db
			.query( "INSERT INTO tag_domains (domain_name) VALUES ($1) RETURNING tag_domain_id", std::move( bindings ) )
			.rows< TagDomainID >()
	};

	if ( rows.empty() )
		co_return http::Response::error( 500, std::format( "Error creating new domain with name {}", name ) );

	const auto domain_id { std::get< 0 >( rows.row( 0 ) ) };
	log::debug( "Created tag domain '{}' as id {}", name, domain_id );

	const auto created { co_await findTagDomain( domain_id, db ) };
	if ( !created )
		co_return http::Response::error( 500, std::format( "Error creating new domain with name {}", name ) );

	simdjson::builder::string_builder builder;
	appendDomainJson( builder, *created );
	auto json { builder.view() };
	if ( json.error() ) throw std::runtime_error( simdjson::error_message( json.error() ) );

	co_return http::Response::json( 200, folly::IOBuf::copyBuffer( *json ) );
}

static http::HTTPTask< http::Response > getTagDomains(
	db::DBContext& db,
	[[maybe_unused]] http::RequestContext context )
{
	const auto domains { co_await listTagDomains( db ) };

	simdjson::builder::string_builder builder;
	builder.start_array();
	bool first { true };
	for ( const auto& domain : domains )
	{
		if ( !first ) builder.append_comma();
		first = false;
		appendDomainJson( builder, domain );
	}
	builder.end_array();

	auto json { builder.view() };
	if ( json.error() ) throw std::runtime_error( simdjson::error_message( json.error() ) );

	co_return http::Response::json( 200, folly::IOBuf::copyBuffer( *json ) );
}

static http::HTTPTask< http::Response > getTagDomainInfo( db::DBContext& db, http::RequestContext context )
{
	const auto tag_domain_id_e { context.parameters().scalar< TagDomainID >( "tag_domain_id" ) };
	if ( !tag_domain_id_e ) co_return http::Response::error( 400, tag_domain_id_e.error().message );

	const auto domain { co_await findTagDomain( *tag_domain_id_e, db ) };
	if ( !domain )
		co_return http::Response::error( 404, std::format( "Domain id {} does not exist", *tag_domain_id_e ) );

	simdjson::builder::string_builder builder;
	appendDomainJson( builder, *domain );
	auto json { builder.view() };
	if ( json.error() ) throw std::runtime_error( simdjson::error_message( json.error() ) );

	co_return http::Response::json( 200, folly::IOBuf::copyBuffer( *json ) );
}

static http::HTTPTask< http::Response > deleteTagDomain( db::DBContext& db, http::RequestContext context )
{
	const auto tag_domain_id_e { context.parameters().scalar< TagDomainID >( "tag_domain_id" ) };
	if ( !tag_domain_id_e ) co_return http::Response::error( 400, tag_domain_id_e.error().message );

	db::DBBindings bindings {};
	bindings.bind( *tag_domain_id_e );

	const auto rows {
		co_await db
			.query( "DELETE FROM tag_domains WHERE tag_domain_id = $1 RETURNING tag_domain_id", std::move( bindings ) )
			.rows< TagDomainID >()
	};

	if ( rows.empty() )
		co_return http::Response::error( 404, std::format( "Failed to find tag domain by id {}", *tag_domain_id_e ) );

	simdjson::builder::string_builder builder;
	builder.start_object();
	builder.append_key_value( "tag_domain_id", std::get< 0 >( rows.row( 0 ) ) );
	builder.end_object();

	auto json { builder.view() };
	if ( json.error() ) throw std::runtime_error( simdjson::error_message( json.error() ) );

	co_return http::Response::json( 200, folly::IOBuf::copyBuffer( *json ) );
}

static http::HTTPTask< http::Response > createRelationshipPairs(
	db::DBContext& db,
	http::RequestContext context,
	const std::string_view a_key,
	const std::string_view b_key,
	std::string insert_sql,
	const std::string_view action )
{
	auto tag_domain_id_e { tagDomainIdParameter( context.parameters() ) };
	if ( !tag_domain_id_e ) co_return std::move( tag_domain_id_e.error() );
	const auto tag_domain_id { *tag_domain_id_e };

	auto body_e { readValidatedJsonBody( context ) };
	if ( !body_e ) co_return std::move( body_e.error() );

	simdjson::dom::parser parser;
	simdjson::dom::array items {};
	try
	{
		if ( parser.parse( body_e->data(), body_e->size() ).get( items ) != simdjson::SUCCESS )
			co_return http::Response::error( 400, "Invalid json object. Expected array as root item" );
	}
	catch ( const simdjson::simdjson_error& e )
	{
		co_return http::Response::error( 400, e.what() );
	}

	std::vector< std::pair< TagID, TagID > > pairs {};
	pairs.reserve( items.size() );
	std::vector< TagID > referenced_tags {};
	referenced_tags.reserve( items.size() * 2 );

	for ( const simdjson::dom::element item : items )
	{
		std::int64_t a {}, b {};
		if ( item[ a_key ].get( a ) != simdjson::SUCCESS )
			co_return http::Response::error( 400, std::format( "Invalid {} item: Must be in TagID form", a_key ) );
		if ( item[ b_key ].get( b ) != simdjson::SUCCESS )
			co_return http::Response::error( 400, std::format( "Invalid {} item: Must be in TagID form", b_key ) );

		auto a_id_e { positiveId< TagID >( a, a_key ) };
		if ( !a_id_e ) co_return std::move( a_id_e.error() );

		auto b_id_e { positiveId< TagID >( b, b_key ) };
		if ( !b_id_e ) co_return std::move( b_id_e.error() );

		const auto a_id { *a_id_e };
		const auto b_id { *b_id_e };
		if ( a_id == b_id )
			co_return http::Response::error(
				400, std::format( "Cannot {} a tag to itself {} == {}", action, a_id, b_id ) );

		pairs.emplace_back( a_id, b_id );
		referenced_tags.push_back( a_id );
		referenced_tags.push_back( b_id );
	}

	auto validation { co_await validateRelationshipIds( tag_domain_id, std::move( referenced_tags ), db ) };
	if ( !validation ) co_return std::move( validation.error() );

	for ( const auto& [ a_id, b_id ] : pairs )
	{
		try
		{
			db::DBBindings bindings {};
			bindings.bind( tag_domain_id );
			bindings.bind( a_id );
			bindings.bind( b_id );
			co_await db.query( insert_sql, std::move( bindings ) ).result();
		}
		catch ( const std::exception& e )
		{
			if ( std::string_view { e.what() }.find( "Cycle detected" ) != std::string_view::npos )
				co_return http::Response::error( 409, std::format( "Error adding {}s: {}", action, e.what() ) );
			co_return http::Response::error( 500, std::format( "Error adding {}s: {}", action, e.what() ) );
		}
	}

	co_return http::Response::json( 200, folly::IOBuf::copyBuffer( std::string_view { "{}" } ) );
}

static http::HTTPTask< http::Response > removeRelationshipPairs(
	db::DBContext& db,
	http::RequestContext context,
	const std::string_view a_key,
	const std::string_view b_key,
	std::string delete_sql )
{
	auto tag_domain_id_e { tagDomainIdParameter( context.parameters() ) };
	if ( !tag_domain_id_e ) co_return std::move( tag_domain_id_e.error() );
	const auto tag_domain_id { *tag_domain_id_e };

	auto body_e { readValidatedJsonBody( context ) };
	if ( !body_e ) co_return std::move( body_e.error() );

	simdjson::dom::parser parser;
	simdjson::dom::array items {};
	try
	{
		if ( parser.parse( body_e->data(), body_e->size() ).get( items ) != simdjson::SUCCESS )
			co_return http::Response::error( 400, "Invalid json object. Expected array as root item" );
	}
	catch ( const simdjson::simdjson_error& e )
	{
		co_return http::Response::error( 400, e.what() );
	}

	for ( const simdjson::dom::element item : items )
	{
		std::int64_t a {}, b {};
		if ( item[ a_key ].get( a ) != simdjson::SUCCESS )
			co_return http::Response::error( 400, std::format( "Invalid {} item: Must be in TagID form", a_key ) );
		if ( item[ b_key ].get( b ) != simdjson::SUCCESS )
			co_return http::Response::error( 400, std::format( "Invalid {} item: Must be in TagID form", b_key ) );

		auto a_id { positiveId< TagID >( a, a_key ) };
		if ( !a_id ) co_return std::move( a_id.error() );

		auto b_id { positiveId< TagID >( b, b_key ) };
		if ( !b_id ) co_return std::move( b_id.error() );

		try
		{
			db::DBBindings bindings {};
			bindings.bind( tag_domain_id );
			bindings.bind( *a_id );
			bindings.bind( *b_id );
			co_await db.query( delete_sql, std::move( bindings ) ).result();
		}
		catch ( const std::exception& e )
		{
			co_return http::Response::error( 500, std::format( "Error removing relationship: {}", e.what() ) );
		}
	}

	co_return http::Response::json( 200, folly::IOBuf::copyBuffer( std::string_view { "{}" } ) );
}

static http::HTTPTask< http::Response > createTagParents( db::DBContext& db, http::RequestContext context )
{
	co_return co_await createRelationshipPairs(
		db,
		std::move( context ),
		"parent_id",
		"child_id",
		"INSERT INTO tag_parents (tag_domain_id, parent_id, child_id) VALUES ($1, $2, $3) "
		"ON CONFLICT(tag_domain_id, parent_id, child_id) DO NOTHING",
		"parent" );
}

static http::HTTPTask< http::Response > createTagAliases( db::DBContext& db, http::RequestContext context )
{
	co_return co_await createRelationshipPairs(
		db,
		std::move( context ),
		"aliased_id",
		"alias_id",
		"INSERT INTO tag_aliases (tag_domain_id, aliased_id, alias_id) VALUES ($1, $2, $3) "
		"ON CONFLICT(tag_domain_id, aliased_id) DO NOTHING",
		"alias" );
}

static http::HTTPTask< http::Response > createTagSiblings(
	[[maybe_unused]] db::DBContext& db,
	[[maybe_unused]] http::RequestContext context )
{
	co_return http::Response::error( 501, "Tag siblings are not currently resolved; this endpoint is disabled" );
}

static http::HTTPTask< http::Response > removeTagParents( db::DBContext& db, http::RequestContext context )
{
	co_return co_await removeRelationshipPairs(
		db,
		std::move( context ),
		"parent_id",
		"child_id",
		"DELETE FROM tag_parents WHERE tag_domain_id = $1 AND parent_id = $2 AND child_id = $3" );
}

static http::HTTPTask< http::Response > removeTagSiblings(
	[[maybe_unused]] db::DBContext& db,
	[[maybe_unused]] http::RequestContext context )
{
	co_return http::Response::error( 501, "Tag siblings are not currently resolved; this endpoint is disabled" );
}

static http::HTTPTask< http::Response > removeTagAliases( db::DBContext& db, http::RequestContext context )
{
	co_return co_await removeRelationshipPairs(
		db,
		std::move( context ),
		"aliased_id",
		"alias_id",
		"DELETE FROM tag_aliases WHERE tag_domain_id = $1 AND aliased_id = $2 AND alias_id = $3" );
}

std::vector< http::RouteDefinition > tagRoutes( db::DBContext& db )
{
	std::vector< http::RouteDefinition > routes {};

	const auto add =
		[ &routes, &db ](
			const proxygen::HTTPMethod method,
			std::string path,
			std::vector< http::PathParameter > path_parameters,
			http::HTTPTask< http::Response > ( *handler )( db::DBContext&, http::RequestContext ) )
	{
		auto bound = [ &db, handler ]( http::RequestContext context ) -> http::HTTPTask< http::Response >
		{ co_return co_await handler( db, std::move( context ) ); };

		routes.push_back(
			http::RouteDefinition {
				http::RouteMetadata {
					method, std::move( path ), std::move( path_parameters ), { apiAuthValidation( db ) } },
				std::move( bound ) } );
	};

	add( proxygen::HTTPMethod::GET, "/tags/{tag_id}/info", { tagIdPathParameter( "tag_id" ) }, getTagInfoFromPath );
	add( proxygen::HTTPMethod::GET, "/tags/info", {}, getTagInfoFromQuery );
	add( proxygen::HTTPMethod::GET,
	     "/tags/{domain_id}/{tag_id}/relationships",
	     { tagDomainIdPathParameter( "domain_id" ), tagIdPathParameter( "tag_id" ) },
	     getTagRelationships );
	add( proxygen::HTTPMethod::GET, "/tags/search", {}, search );
	add( proxygen::HTTPMethod::GET, "/tags/autocomplete", {}, autocomplete );
	add( proxygen::HTTPMethod::POST, "/tags/create", {}, createTagsFromRequest );
	add( proxygen::HTTPMethod::POST, "/tags/domain/create", {}, createTagDomain );
	add( proxygen::HTTPMethod::GET, "/tags/domain/list", {}, getTagDomains );
	add( proxygen::HTTPMethod::GET,
	     "/tags/domain/{tag_domain_id}/info",
	     { tagDomainIdPathParameter( "tag_domain_id" ) },
	     getTagDomainInfo );
	add( proxygen::HTTPMethod::DELETE,
	     "/tags/domain/{tag_domain_id}/delete",
	     { tagDomainIdPathParameter( "tag_domain_id" ) },
	     deleteTagDomain );
	add( proxygen::HTTPMethod::POST, "/tags/parents/create", {}, createTagParents );
	add( proxygen::HTTPMethod::POST, "/tags/alias/create", {}, createTagAliases );
	add( proxygen::HTTPMethod::POST, "/tags/siblings/create", {}, createTagSiblings );
	add( proxygen::HTTPMethod::POST, "/tags/parents/remove", {}, removeTagParents );
	add( proxygen::HTTPMethod::POST, "/tags/siblings/remove", {}, removeTagSiblings );
	add( proxygen::HTTPMethod::POST, "/tags/alias/remove", {}, removeTagAliases );

	return routes;
}

} // namespace idhan::httpapi
