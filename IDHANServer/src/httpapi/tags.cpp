#include "tags.hpp"

#include <algorithm>
#include <format>
#include <simdjson.h>

#include "db/tagDomainCounts.hpp"
#include "idValidation.hpp"
#include "idhan/db/DBContext.hpp"
#include "idhan/http/routing/ReflectedRoutes.hpp"
#include "jsonBody.hpp"
#include "logging/format_ns.hpp"
#include "logging/log.hpp"
#include "tagHelpers.hpp"

namespace idhan::httpapi
{

static http::HTTPTask< std::expected< std::vector< TagDomainID >, http::Response > > tagCountDomains(
	db::DBContext& db,
	std::vector< TagDomainID > domains );

static http::HTTPTask< http::JsonResult< TagInfoResponse > > getTagInfo(
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

	TagInfoResponse response {
		tag_id, { namespace_id, namespace_text }, { subtag_text }, storage_count, display_count, {}
	};
	response.domain_counts.reserve( counts.size() );
	for ( const auto& count : counts )
		response.domain_counts.push_back( { count.domain_id, count.storage_count, count.display_count } );

	co_return http::JsonResponse< TagInfoResponse > { std::move( response ) };
}

http::HTTPTask< http::JsonResult< TagInfoResponse > > getTagInfoFromPath(
	db::DBContext& db,
	http::RequestContext context,
	http::Query< GetTagInfoFromPathQuery > query )
{
	const auto tag_id { *context.parameters().scalar< TagID >( "tag_id" ) };

	auto domains { co_await tagCountDomains( db, std::move( query.value.tag_domains ) ) };
	if ( !domains ) co_return std::move( domains.error() );

	co_return co_await getTagInfo( db, tag_id, std::move( *domains ) );
}

http::HTTPTask< http::JsonResult< TagInfoResponse > > getTagInfoFromQuery(
	db::DBContext& db,
	http::Query< GetTagInfoFromQueryQuery > query )
{
	const auto tag_id { query.value.tag_id ? query.value.tag_id : query.value.tag_ids };
	if ( !tag_id ) co_return http::Response::error( 400, "tag_id or tag_ids query parameter is required" );

	auto domains { co_await tagCountDomains( db, std::move( query.value.tag_domains ) ) };
	if ( !domains ) co_return std::move( domains.error() );

	co_return co_await getTagInfo( db, *tag_id, std::move( *domains ) );
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

http::HTTPTask< http::JsonResult< TagRelationshipsResponse > > getTagRelationships(
	db::DBContext& db,
	http::RequestContext context )
{
	const auto domain_id_e { context.parameters().scalar< TagDomainID >( "domain_id" ) };
	if ( !domain_id_e ) co_return http::Response::error( 400, domain_id_e.error().message );
	const auto tag_id_e { context.parameters().scalar< TagID >( "tag_id" ) };
	if ( !tag_id_e ) co_return http::Response::error( 400, tag_id_e.error().message );

	const auto tag_id { *tag_id_e };
	const auto tag_domain_id { *domain_id_e };

	auto parents { co_await relationshipQuery(
		db, "SELECT parent_id FROM tag_parents WHERE child_id = $1 AND tag_domain_id = $2", tag_id, tag_domain_id ) };
	auto children { co_await relationshipQuery(
		db, "SELECT child_id FROM tag_parents WHERE parent_id = $1 AND tag_domain_id = $2", tag_id, tag_domain_id ) };
	auto older_siblings { co_await relationshipQuery(
		db, "SELECT older_id FROM tag_siblings WHERE younger_id = $1 AND tag_domain_id = $2", tag_id, tag_domain_id ) };
	auto younger_siblings { co_await relationshipQuery(
		db, "SELECT younger_id FROM tag_siblings WHERE older_id = $1 AND tag_domain_id = $2", tag_id, tag_domain_id ) };
	auto aliases { co_await relationshipQuery(
		db, "SELECT alias_id FROM tag_aliases WHERE aliased_id = $1 AND tag_domain_id = $2", tag_id, tag_domain_id ) };
	auto aliased { co_await relationshipQuery(
		db, "SELECT aliased_id FROM tag_aliases WHERE alias_id = $1 AND tag_domain_id = $2", tag_id, tag_domain_id ) };

	co_return http::JsonResponse< TagRelationshipsResponse > {
		{ std::move( parents ),
		  std::move( children ),
		  std::move( older_siblings ),
		  std::move( younger_siblings ),
		  std::move( aliases ),
		  std::move( aliased ) }
	};
}

http::HTTPTask< http::JsonResult< TagSearchResponse > > searchNativeTags(
	db::DBContext& db,
	http::Query< TagSearchParameters > query )
{
	db::DBBindings bindings {};
	bindings.bind( query.value.tag );

	const auto rows {
		co_await db
			.query( "SELECT tag_id FROM tags WHERE tag_text = CASEFOLD(normalize($1, NFC))", std::move( bindings ) )
			.rows< TagID >()
	};

	if ( !rows.empty() ) co_return http::JsonResponse< TagSearchResponse > { { std::get< 0 >( rows.row( 0 ) ), true } };

	co_return http::JsonResponse< TagSearchResponse > {};
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
	std::vector< TagDomainID > domains )
{
	std::ranges::sort( domains );
	const auto duplicates { std::ranges::unique( domains ) };
	domains.erase( duplicates.begin(), duplicates.end() );

	for ( const auto domain_id : domains )
	{
		if ( domain_id <= 0 )
			co_return std::unexpected( http::Response::error( 400, "tag_domains must contain positive IDs" ) );

		const auto domain { co_await findTagDomain( domain_id, db ) };
		if ( !domain )
			co_return std::unexpected(
				http::Response::error( 404, std::format( "Tag domain {} does not exist", domain_id ) ) );
	}

	co_return domains;
}

http::HTTPTask< http::JsonResult< std::vector< SimilarTag > > > autocomplete(
	db::DBContext& db,
	http::Query< AutocompleteQuery > query )
{
	auto selected_domains { std::move( query.value.tag_domains ) };
	if ( selected_domains.empty() && query.value.tag_domain ) selected_domains.push_back( *query.value.tag_domain );

	auto domains { co_await tagCountDomains( db, std::move( selected_domains ) ) };
	if ( !domains ) co_return std::move( domains.error() );

	const auto display {
		query.value.tag_display_type == TagCountDisplayType::storage ?
			HydrusDisplayType::STORED :
			HydrusDisplayType::DISPLAY
	};
	auto results { co_await getSimilarTags(
		db, query.value.tag, query.value.limit, query.value.include_unused, display, std::move( *domains ) ) };

	co_return http::JsonResponse< std::vector< SimilarTag > > { std::move( results ) };
}

http::HTTPTask< http::JsonResult< std::vector< CreatedTagResponse > > > createTagsFromRequest(
	db::DBContext& db,
	http::RequestContext context )
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

	auto tag_ids_e { co_await createTagsFromPairs( std::move( tag_pairs ), db ) };
	if ( !tag_ids_e.has_value() ) co_return std::move( tag_ids_e.error() );

	// value() requires a copyable error type; Response is move-only.
	const auto& tag_ids { *tag_ids_e };
	std::vector< CreatedTagResponse > created;
	created.reserve( tag_ids.size() );
	for ( const auto tag_id : tag_ids ) created.push_back( { tag_id } );

	co_return http::JsonResponse< std::vector< CreatedTagResponse > > { std::move( created ) };
}

http::HTTPTask< http::JsonResult< TagDomainInfo > > createTagDomain( db::DBContext& db, http::RequestContext context )
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

	const auto existing { co_await findTagDomain( name, db ) };
	if ( existing.has_value() )
	{
		log::debug( "Found existing tag domain with name '{}'", name );

		co_return http::serializeResponse( http::JsonResponse< TagDomainInfo, 409 > { existing.value() } );
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

	co_return http::JsonResponse< TagDomainInfo > { created.value() };
}

http::HTTPTask< http::JsonResult< std::vector< TagDomainInfo > > > getTagDomains(
	db::DBContext& db,
	[[maybe_unused]] http::RequestContext context )
{
	auto domains { co_await listTagDomains( db ) };

	co_return http::JsonResponse< std::vector< TagDomainInfo > > { std::move( domains ) };
}

http::HTTPTask< http::JsonResult< TagDomainInfo > > getTagDomainInfo( db::DBContext& db, http::RequestContext context )
{
	const auto tag_domain_id_e { context.parameters().scalar< TagDomainID >( "tag_domain_id" ) };
	if ( !tag_domain_id_e ) co_return http::Response::error( 400, tag_domain_id_e.error().message );

	const auto domain { co_await findTagDomain( *tag_domain_id_e, db ) };
	if ( !domain )
		co_return http::Response::error( 404, std::format( "Domain id {} does not exist", *tag_domain_id_e ) );

	co_return http::JsonResponse< TagDomainInfo > { domain.value() };
}

http::HTTPTask< http::JsonResult< DeletedTagDomainResponse > > deleteTagDomain(
	db::DBContext& db,
	http::RequestContext context )
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

	co_return http::JsonResponse< DeletedTagDomainResponse > { { std::get< 0 >( rows.row( 0 ) ) } };
}

static http::HTTPTask< http::JsonResult< TagUpdateResponse > > createRelationshipPairs(
	db::DBContext& db,
	http::RequestContext context,
	const TagDomainID tag_domain_id,
	const std::string_view a_key,
	const std::string_view b_key,
	std::string insert_sql,
	const std::string_view action )
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

	try
	{
		auto transaction { co_await db.transaction() };
		for ( const auto& [ a_id, b_id ] : pairs )
		{
			db::DBBindings bindings {};
			bindings.bind( tag_domain_id );
			bindings.bind( a_id );
			bindings.bind( b_id );
			co_await transaction.query( insert_sql, std::move( bindings ) ).result();
		}
		co_await transaction.commit();
	}
	catch ( const std::exception& e )
	{
		if ( std::string_view { e.what() }.find( "Cycle detected" ) != std::string_view::npos )
			co_return http::Response::error( 409, std::format( "Error adding {}s: {}", action, e.what() ) );

		co_return http::Response::error( 500, std::format( "Error adding {}s: {}", action, e.what() ) );
	}

	co_return http::JsonResponse< TagUpdateResponse > {};
}

static http::HTTPTask< http::JsonResult< TagUpdateResponse > > removeRelationshipPairs(
	db::DBContext& db,
	http::RequestContext context,
	const TagDomainID tag_domain_id,
	const std::string_view a_key,
	const std::string_view b_key,
	std::string delete_sql )
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

	std::vector< std::pair< TagID, TagID > > pairs {};
	pairs.reserve( items.size() );
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

		pairs.emplace_back( *a_id, *b_id );
	}

	try
	{
		auto transaction { co_await db.transaction() };
		for ( const auto& [ a_id, b_id ] : pairs )
		{
			db::DBBindings bindings {};
			bindings.bind( tag_domain_id );
			bindings.bind( a_id );
			bindings.bind( b_id );
			co_await transaction.query( delete_sql, std::move( bindings ) ).result();
		}
		co_await transaction.commit();
	}
	catch ( const std::exception& e )
	{
		co_return http::Response::error( 500, std::format( "Error removing relationship: {}", e.what() ) );
	}

	co_return http::JsonResponse< TagUpdateResponse > {};
}

http::HTTPTask< http::JsonResult< TagUpdateResponse > > createTagParents(
	db::DBContext& db,
	http::RequestContext context,
	http::Query< CreateTagParentsQuery > query )
{
	co_return co_await createRelationshipPairs(
		db,
		std::move( context ),
		query.value.tag_domain_id,
		"parent_id",
		"child_id",
		"INSERT INTO tag_parents (tag_domain_id, parent_id, child_id) VALUES ($1, $2, $3) "
		"ON CONFLICT(tag_domain_id, parent_id, child_id) DO NOTHING",
		"parent" );
}

http::HTTPTask< http::JsonResult< TagUpdateResponse > > createTagAliases(
	db::DBContext& db,
	http::RequestContext context,
	http::Query< CreateTagAliasesQuery > query )
{
	co_return co_await createRelationshipPairs(
		db,
		std::move( context ),
		query.value.tag_domain_id,
		"aliased_id",
		"alias_id",
		"INSERT INTO tag_aliases (tag_domain_id, aliased_id, alias_id) VALUES ($1, $2, $3) "
		"ON CONFLICT(tag_domain_id, aliased_id) DO NOTHING",
		"alias" );
}

http::HTTPTask< http::Response > createTagSiblings(
	[[maybe_unused]] db::DBContext& db,
	[[maybe_unused]] http::RequestContext context )
{
	co_return http::Response::error( 501, "Tag siblings are not currently resolved; this endpoint is disabled" );
}

http::HTTPTask< http::JsonResult< TagUpdateResponse > > removeTagParents(
	db::DBContext& db,
	http::RequestContext context,
	http::Query< RemoveTagParentsQuery > query )
{
	co_return co_await removeRelationshipPairs(
		db,
		std::move( context ),
		query.value.tag_domain_id,
		"parent_id",
		"child_id",
		"DELETE FROM tag_parents WHERE tag_domain_id = $1 AND parent_id = $2 AND child_id = $3" );
}

http::HTTPTask< http::Response > removeTagSiblings(
	[[maybe_unused]] db::DBContext& db,
	[[maybe_unused]] http::RequestContext context )
{
	co_return http::Response::error( 501, "Tag siblings are not currently resolved; this endpoint is disabled" );
}

http::HTTPTask< http::JsonResult< TagUpdateResponse > > removeTagAliases(
	db::DBContext& db,
	http::RequestContext context,
	http::Query< RemoveTagAliasesQuery > query )
{
	co_return co_await removeRelationshipPairs(
		db,
		std::move( context ),
		query.value.tag_domain_id,
		"aliased_id",
		"alias_id",
		"DELETE FROM tag_aliases WHERE tag_domain_id = $1 AND aliased_id = $2 AND alias_id = $3" );
}

} // namespace idhan::httpapi
