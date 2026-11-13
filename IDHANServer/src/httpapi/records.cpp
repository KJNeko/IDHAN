#include "records.hpp"

#include <folly/coro/Collect.h>
#include <folly/io/IOBuf.h>

#include <cerrno>
#include <format>
#include <limits>
#include <simdjson.h>
#include <unordered_map>
#include <unordered_set>

#include "apiAuth.hpp"
#include "db/records.hpp"
#include "filesystem/io/IOUring.hpp"
#include "filesystem/recordFilePath.hpp"
#include "helpers.hpp"
#include "idValidation.hpp"
#include "idhan/SHA256.hpp"
#include "idhan/db/DBContext.hpp"
#include "idhan/db/DBTransaction.hpp"
#include "idhan/http/routing/ReflectedRoutes.hpp"
#include "jsonBody.hpp"
#include "logging/log.hpp"
#include "recordValidator.hpp"
#include "splitTag.hpp"
#include "tagHelpers.hpp"
#include "urls/UrlDomain.hpp"
#include "urls/UrlNormalization.hpp"

namespace idhan::httpapi
{

[[nodiscard]] static std::expected< SHA256, http::Response > parseSha256Hex( const std::string_view text )
{
	constexpr std::size_t hex_length { SHA256::size() * 2 };
	if ( text.size() != hex_length )
		return std::unexpected(
			http::Response::error(
				400, std::format( "A sha256 is {} hex characters, got {}", hex_length, text.size() ) ) );

	const auto hash { SHA256::fromHex( text ) };
	if ( !hash )
		return std::unexpected( http::Response::error( 400, std::format( "Invalid sha256: {}", hash.error() ) ) );

	return *hash;
}

runtime::Task< std::optional< RecordID > > findRecord( const SHA256& sha256, db::DBContext& db )
{
	co_return co_await db::recordForHash( sha256, db );
}

runtime::Task< std::expected< RecordID, http::Response > > createRecord( const SHA256& sha256, db::DBContext& db )
{
	auto record { co_await db::ensureRecord( sha256, db ) };
	if ( !record ) co_return std::unexpected( http::Response::error( 500, record.error() ) );

	co_return *record;
}

runtime::Task< std::expected< RecordID, http::Response > > createRecord(
	const SHA256& sha256,
	db::DBTransaction& transaction )
{
	auto record { co_await db::ensureRecord( sha256, transaction ) };
	if ( !record ) co_return std::unexpected( http::Response::error( 500, record.error() ) );

	co_return *record;
}

static runtime::Task< std::vector< RecordID > > massCreateRecord(
	const std::vector< SHA256 >& sha256s,
	db::DBContext& db )
{
	if ( sha256s.empty() ) co_return {};

	std::vector< std::vector< std::byte > > raw {};
	raw.reserve( sha256s.size() );
	for ( const auto& sha256 : sha256s ) raw.emplace_back( sha256.data().begin(), sha256.data().end() );

	db::DBBindings insert_bindings {};
	insert_bindings.bind( raw );
	co_await db
		.query(
			"INSERT INTO records (sha256) SELECT * FROM UNNEST($1) ON CONFLICT DO NOTHING",
			std::move( insert_bindings ) )
		.result();

	db::DBBindings select_bindings {};
	select_bindings.bind( raw );
	const auto rows {
		co_await db
			.query( "SELECT record_id, sha256 FROM records WHERE sha256 = ANY($1)", std::move( select_bindings ) )
			.rows< RecordID, std::span< const std::byte > >()
	};

	std::unordered_map< SHA256, RecordID > record_id_map {};
	for ( const auto& [ record_id, sha256_bytes ] : rows )
		record_id_map.emplace( SHA256::fromBuffer( sha256_bytes ), record_id );

	std::vector< RecordID > record_ids {};
	record_ids.reserve( sha256s.size() );
	for ( const auto& sha256 : sha256s )
	{
		const auto it { record_id_map.find( sha256 ) };
		if ( it == record_id_map.end() ) co_return {};
		record_ids.push_back( it->second );
	}

	co_return record_ids;
}

struct TagPair
{
	std::optional< TagID > tag_id {};
	std::variant< NamespaceID, std::string > tag_namespace {};
	std::string tag_subtag {};
};

[[nodiscard]] static std::expected< TagPair, http::Response > parseTagPair( const simdjson::dom::element item )
{
	if ( item.is_object() )
	{
		std::int64_t namespace_int {};
		std::string_view namespace_string {}, subtag {};

		if ( item[ "subtag" ].get( subtag ) != simdjson::SUCCESS )
			return std::unexpected( http::Response::error( 400, "Invalid tag subtag: Subtag was not a string" ) );

		TagPair pair {};
		pair.tag_subtag = subtag;

		if ( item[ "namespace" ].get( namespace_int ) == simdjson::SUCCESS )
		{
			auto id { positiveId< NamespaceID >( namespace_int, "namespace" ) };
			if ( !id ) return std::unexpected( std::move( id.error() ) );

			pair.tag_namespace = *id;
		}
		else if ( item[ "namespace" ].get( namespace_string ) == simdjson::SUCCESS )
			pair.tag_namespace = std::string { namespace_string };
		else
			return std::unexpected(
				http::Response::error( 400, "Invalid tag namespace: Namespace was neither numeric nor string" ) );

		return pair;
	}

	std::int64_t tag_id {};
	if ( item.get_int64().get( tag_id ) == simdjson::SUCCESS )
	{
		if ( tag_id <= 0 || tag_id > std::numeric_limits< TagID >::max() )
			return std::unexpected(
				http::Response::error( 400, std::format( "Invalid tag id {}: out of range", tag_id ) ) );

		TagPair pair {};
		pair.tag_id = static_cast< TagID >( tag_id );
		return pair;
	}

	std::string_view text {};
	if ( item.get_string().get( text ) == simdjson::SUCCESS )
	{
		const auto [ tag_namespace, tag_subtag ] { splitTag( std::string { text } ) };
		TagPair pair {};
		pair.tag_namespace = tag_namespace;
		pair.tag_subtag = tag_subtag;
		return pair;
	}

	return std::unexpected( http::Response::error( 400, "Invalid type format for json" ) );
}

[[nodiscard]] static std::expected< std::vector< TagPair >, http::Response > parseTagPairs(
	const simdjson::dom::array array )
{
	std::vector< TagPair > tags {};
	tags.reserve( array.size() );

	for ( const simdjson::dom::element item : array )
	{
		auto pair { parseTagPair( item ) };
		if ( !pair ) return std::unexpected( std::move( pair.error() ) );
		tags.push_back( std::move( *pair ) );
	}

	return tags;
}

static runtime::Task< std::expected< std::vector< TagID >, http::Response > > resolveTagPairIds(
	const std::vector< TagPair >& pairs,
	db::DBContext& db )
{
	std::vector< TagID > ids( pairs.size(), INVALID_TAG_ID );

	std::vector< std::pair< std::string, std::string > > string_pairs {};
	std::vector< std::size_t > string_indices {};
	std::vector< std::size_t > namespace_id_indices {};

	for ( std::size_t i = 0; i < pairs.size(); ++i )
	{
		const auto& pair { pairs[ i ] };

		if ( pair.tag_id )
			ids[ i ] = *pair.tag_id;
		else if ( std::holds_alternative< std::string >( pair.tag_namespace ) )
		{
			string_indices.push_back( i );
			string_pairs.emplace_back( std::get< std::string >( pair.tag_namespace ), pair.tag_subtag );
		}
		else
			namespace_id_indices.push_back( i );
	}

	if ( !string_pairs.empty() )
	{
		auto batch_ids { co_await createTagsFromPairs( string_pairs, db ) };
		if ( !batch_ids ) co_return std::unexpected( std::move( batch_ids.error() ) );

		for ( std::size_t k = 0; k < string_indices.size(); ++k ) ids[ string_indices[ k ] ] = ( *batch_ids )[ k ];
	}

	if ( !namespace_id_indices.empty() )
	{
		std::vector< runtime::Task< std::expected< TagID, http::Response > > > tasks {};
		tasks.reserve( namespace_id_indices.size() );
		for ( const auto index : namespace_id_indices )
			tasks.push_back(
				createTag( std::get< NamespaceID >( pairs[ index ].tag_namespace ), pairs[ index ].tag_subtag, db ) );

		auto results { co_await folly::coro::collectAllRange( std::move( tasks ) ) };

		for ( std::size_t k = 0; k < namespace_id_indices.size(); ++k )
		{
			if ( !results[ k ] ) co_return std::unexpected( std::move( results[ k ].error() ) );
			ids[ namespace_id_indices[ k ] ] = *results[ k ];
		}
	}

	for ( const auto id : ids )
		if ( id <= 0 )
			co_return std::unexpected(
				http::Response::error( 400, std::format( "Tag ID was not valid. Must be tag_id > 0; Was {}", id ) ) );

	co_return ids;
}

static runtime::Task< std::expected< std::vector< TagID >, http::Response > > resolveOwnedTagPairIds(
	std::vector< TagPair > pairs,
	db::DBContext& db )
{
	co_return co_await resolveTagPairIds( pairs, db );
}

static runtime::Task<> removeTagMappings(
	const RecordID record_id,
	std::vector< TagID > tag_ids,
	const TagDomainID tag_domain_id,
	db::DBContext& db )
{
	if ( tag_ids.empty() ) co_return;

	db::DBBindings bindings {};
	bindings.bind( record_id );
	bindings.bind( tag_ids );
	bindings.bind( tag_domain_id );

	co_await db
		.query(
			"DELETE FROM tag_mappings WHERE record_id = $1 AND tag_id = ANY($2) AND tag_domain_id = $3",
			std::move( bindings ) )
		.result();
}

static runtime::Task< std::expected< void, http::Response > > addTagsToRecord(
	const RecordID record_id,
	std::vector< TagID > tag_ids,
	const TagDomainID tag_domain_id,
	db::DBContext& db )
{
	try
	{
		db::DBBindings bindings {};
		bindings.bind( record_id );
		bindings.bind( tag_ids );
		bindings.bind( tag_domain_id );
		co_await db
			.query(
				"INSERT INTO tag_mappings (record_id, tag_id, tag_domain_id) "
				"SELECT $1, t, $3 FROM UNNEST($2) AS t ON CONFLICT DO NOTHING",
				std::move( bindings ) )
			.result();
	}
	catch ( const std::exception& e )
	{
		co_return std::unexpected( http::Response::error( 500, std::format( "Error adding tags: {}", e.what() ) ) );
	}

	co_return {};
}

static runtime::Task< std::expected< void, http::Response > > addTagsToRecords(
	std::vector< RecordID > record_ids,
	std::vector< TagID > tag_ids,
	const TagDomainID tag_domain_id,
	db::DBContext& db )
{
	if ( record_ids.empty() || tag_ids.empty() ) co_return {};

	try
	{
		db::DBBindings bindings {};
		bindings.bind( record_ids );
		bindings.bind( tag_ids );
		bindings.bind( tag_domain_id );
		co_await db
			.query(
				"INSERT INTO tag_mappings (record_id, tag_id, tag_domain_id) "
				"SELECT r, t, $3 FROM UNNEST($1) AS r CROSS JOIN UNNEST($2) AS t ON CONFLICT DO NOTHING",
				std::move( bindings ) )
			.result();
	}
	catch ( const std::exception& e )
	{
		co_return std::unexpected( http::Response::error( 500, std::format( "Error adding tags: {}", e.what() ) ) );
	}

	co_return {};
}

static runtime::Task< std::expected< void, http::Response > > addTagSetsToRecords(
	const std::vector< RecordID >& record_ids,
	const std::vector< std::vector< TagID > >& tag_sets,
	const TagDomainID tag_domain_id,
	db::DBContext& db )
{
	std::vector< RecordID > flat_record_ids {};
	std::vector< TagID > flat_tag_ids {};

	for ( std::size_t i = 0; i < record_ids.size(); ++i )
		for ( const auto tag_id : tag_sets[ i ] )
		{
			flat_record_ids.push_back( record_ids[ i ] );
			flat_tag_ids.push_back( tag_id );
		}

	if ( flat_record_ids.empty() ) co_return {};

	try
	{
		db::DBBindings bindings {};
		bindings.bind( flat_record_ids );
		bindings.bind( flat_tag_ids );
		bindings.bind( tag_domain_id );
		co_await db
			.query(
				"INSERT INTO tag_mappings (record_id, tag_id, tag_domain_id) "
				"SELECT pairs.record_id, pairs.tag_id, $3 "
				"FROM UNNEST($1, $2) AS pairs(record_id, tag_id) ON CONFLICT DO NOTHING",
				std::move( bindings ) )
			.result();
	}
	catch ( const std::exception& e )
	{
		co_return std::unexpected( http::Response::error( 500, std::format( "Error adding tags: {}", e.what() ) ) );
	}

	co_return {};
}

static runtime::Task< std::expected< NoteID, http::Response > > findOrCreateNote( std::string text, db::DBContext& db )
{
	db::DBBindings insert_bindings {};
	insert_bindings.bind( text );
	const auto created {
		co_await db
			.query(
				"INSERT INTO notes (note) VALUES ($1) ON CONFLICT (note) DO NOTHING RETURNING note_id",
				std::move( insert_bindings ) )
			.rows< NoteID >()
	};

	if ( !created.empty() ) co_return std::get< 0 >( created.row( 0 ) );

	db::DBBindings select_bindings {};
	select_bindings.bind( text );
	const auto existing {
		co_await db.query( "SELECT note_id FROM notes WHERE note = $1", std::move( select_bindings ) ).rows< NoteID >()
	};

	if ( !existing.empty() ) co_return std::get< 0 >( existing.row( 0 ) );

	co_return std::unexpected( http::Response::error( 500, "Failed to create or find note" ) );
}

static runtime::Task< http::JsonResponse< std::vector< RecordNoteResponse > > > recordNotesResponse(
	const RecordID record_id,
	db::DBContext& db )
{
	db::DBBindings bindings {};
	bindings.bind( record_id );
	const auto rows {
		co_await db
			.query(
				"SELECT note, note_id FROM record_notes JOIN notes USING (note_id) WHERE record_id = $1",
				std::move( bindings ) )
			.rows< std::string, NoteID >()
	};

	std::vector< RecordNoteResponse > response;
	response.reserve( rows.size() );
	for ( const auto& [ note, note_id ] : rows ) response.push_back( { note_id, note } );

	co_return http::JsonResponse< std::vector< RecordNoteResponse > > { std::move( response ) };
}

http::HTTPTask< http::JsonResult< std::vector< RecordNoteResponse > > > getNotes(
	db::DBContext& db,
	http::RequestContext context )
{
	const auto record_id { *context.parameters().scalar< RecordID >( "record_id" ) };
	co_return co_await recordNotesResponse( record_id, db );
}

http::HTTPTask< http::JsonResult< std::vector< RecordNoteResponse > > > addNote(
	db::DBContext& db,
	http::RequestContext context )
{
	const auto record_id { *context.parameters().scalar< RecordID >( "record_id" ) };

	const auto content_type { context.message().getHeaders().getSingleOrEmpty( "Content-Type" ) };
	if ( !content_type.empty() && !content_type.starts_with( "text/plain" ) )
		co_return http::Response::error( 400, "Content type must be text/plain" );

	auto text { readRequestBody( context ) };
	if ( !text ) co_return std::move( text.error() );

	auto note_id { co_await findOrCreateNote( *text, db ) };
	if ( !note_id ) co_return std::move( note_id.error() );

	db::DBBindings bindings {};
	bindings.bind( record_id );
	bindings.bind( *note_id );
	co_await db
		.query(
			"INSERT INTO record_notes (record_id, note_id) VALUES ($1, $2) ON CONFLICT DO NOTHING",
			std::move( bindings ) )
		.result();

	co_return co_await recordNotesResponse( record_id, db );
}

http::HTTPTask< http::JsonResult< std::vector< RecordNoteResponse > > > removeNote(
	db::DBContext& db,
	http::RequestContext context )
{
	const auto record_id { *context.parameters().scalar< RecordID >( "record_id" ) };
	const auto note_id_e { context.parameters().scalar< NoteID >( "note_id" ) };
	if ( !note_id_e ) co_return http::Response::error( 400, note_id_e.error().message );

	db::DBBindings bindings {};
	bindings.bind( record_id );
	bindings.bind( *note_id_e );
	co_await db.query( "DELETE FROM record_notes WHERE record_id = $1 AND note_id = $2", std::move( bindings ) )
		.result();

	co_return co_await recordNotesResponse( record_id, db );
}

http::HTTPTask< http::JsonResult< std::vector< std::string > > > fetchUrls(
	db::DBContext& db,
	http::RequestContext context )
{
	const auto record_id { *context.parameters().scalar< RecordID >( "record_id" ) };

	db::DBBindings bindings {};
	bindings.bind( record_id );
	const auto rows {
		co_await db
			.query(
				"SELECT url FROM url_mappings JOIN urls USING (url_id) WHERE record_id = $1 ORDER BY url",
				std::move( bindings ) )
			.rows< std::string >()
	};

	std::vector< std::string > response;
	response.reserve( rows.size() );
	for ( const auto& [ url ] : rows ) response.push_back( url );

	co_return http::JsonResponse< std::vector< std::string > > { std::move( response ) };
}

http::HTTPTask< http::JsonResult< RecordUrlsAddedResponse > > addUrls( db::DBContext& db, http::RequestContext context )
{
	const auto record_id { *context.parameters().scalar< RecordID >( "record_id" ) };

	auto body_e { readValidatedJsonBody( context ) };
	if ( !body_e ) co_return std::move( body_e.error() );

	simdjson::dom::parser parser;
	simdjson::dom::array urls_array {};
	try
	{
		const simdjson::dom::element root { parser.parse( body_e->data(), body_e->size() ) };
		if ( !root.is_object() )
			co_return http::Response::error( 400, "Invalid json object. Expected object as root item" );
		if ( root[ "urls" ].get( urls_array ) != simdjson::SUCCESS )
			co_return http::Response::error( 400, "No urls array in json" );
	}
	catch ( const simdjson::simdjson_error& e )
	{
		co_return http::Response::error( 400, e.what() );
	}

	if ( urls_array.size() == 0 ) co_return http::JsonResponse< RecordUrlsAddedResponse > {};

	std::vector< std::string > submitted {};
	submitted.reserve( urls_array.size() );
	for ( const simdjson::dom::element item : urls_array )
	{
		std::string_view url {};
		if ( item.get_string().get( url ) != simdjson::SUCCESS )
			co_return http::Response::error( 400, "Invalid item in urls array: Expected string" );
		submitted.emplace_back( url );
	}

	auto urls_e { co_await idhan::helpers::normalizeUrls( std::move( submitted ) ) };
	if ( !urls_e.has_value() )
	{
		const auto& error { urls_e.error() };
		co_return http::Response::error( idhan::helpers::httpStatus( error ), error.message );
	}

	auto& urls { urls_e.value() };

	std::vector< std::string > domains {};
	domains.reserve( urls.size() );
	for ( const auto& url : urls ) domains.push_back( idhan::helpers::extractDomain( url ) );

	db::DBBindings domain_bindings {};
	domain_bindings.bind( domains );
	co_await db
		.query(
			"INSERT INTO url_domains (url_domain) SELECT DISTINCT unnest($1) ON CONFLICT (url_domain) DO NOTHING",
			std::move( domain_bindings ) )
		.result();

	static constexpr char query[] {
#embed "sql/insert_record_urls.sql" suffix(, )

		'\0'
	};

	db::DBBindings url_bindings {};
	url_bindings.bind( urls );
	url_bindings.bind( domains );
	co_await db.query( query, std::move( url_bindings ) ).result();

	db::DBBindings mapping_bindings {};
	mapping_bindings.bind( urls );
	mapping_bindings.bind( record_id );
	co_await db
		.query(
			"INSERT INTO url_mappings (url_id, record_id) "
			"SELECT url_id, $2 FROM urls WHERE url = ANY($1) ON CONFLICT DO NOTHING",
			std::move( mapping_bindings ) )
		.result();

	co_return http::JsonResponse< RecordUrlsAddedResponse > {};
}

http::HTTPTask< std::variant< http::EmptyResponse< 200 >, http::Response > > removeUrls(
	db::DBContext& db,
	http::RequestContext context )
{
	const auto record_id { *context.parameters().scalar< RecordID >( "record_id" ) };

	auto body_e { readValidatedJsonBody( context ) };
	if ( !body_e ) co_return std::move( body_e.error() );

	simdjson::dom::parser parser;
	simdjson::dom::array urls_array {};
	try
	{
		const simdjson::dom::element root { parser.parse( body_e->data(), body_e->size() ) };
		if ( !root.is_object() )
			co_return http::Response::error( 400, "Invalid json object. Expected object as root item" );
		if ( root[ "urls" ].get( urls_array ) != simdjson::SUCCESS )
			co_return http::Response::error( 400, "No urls array in json" );
	}
	catch ( const simdjson::simdjson_error& e )
	{
		co_return http::Response::error( 400, e.what() );
	}

	std::vector< std::string > urls {};
	urls.reserve( urls_array.size() );
	for ( const simdjson::dom::element item : urls_array )
	{
		std::string_view url {};
		if ( item.get_string().get( url ) != simdjson::SUCCESS )
			co_return http::Response::error( 400, "Invalid item in urls array: Expected string" );
		urls.emplace_back( url );
	}

	auto spellings_e { co_await idhan::helpers::urlSpellings( std::move( urls ) ) };
	if ( !spellings_e.has_value() )
	{
		const auto& error { spellings_e.error() };
		co_return http::Response::error( idhan::helpers::httpStatus( error ), error.message );
	}

	db::DBBindings bindings {};
	bindings.bind( spellings_e.value() );
	bindings.bind( record_id );
	co_await db
		.query(
			"DELETE FROM url_mappings um USING urls u "
			"WHERE u.url = ANY($1) AND um.url_id = u.url_id AND um.record_id = $2",
			std::move( bindings ) )
		.result();

	co_return http::EmptyResponse< 200 > {};
}

[[nodiscard]] static std::vector< RecordDomainTagsResponse > recordDomainTagGroups(
	std::unordered_map< TagDomainID, std::vector< TagID > > domain_map )
{
	std::vector< RecordDomainTagsResponse > groups;
	groups.reserve( domain_map.size() );
	for ( auto& [ domain_id, tag_ids ] : domain_map ) groups.push_back( { domain_id, std::move( tag_ids ) } );

	return groups;
}

http::HTTPTask< http::JsonResult< std::vector< RecordDomainTagsResponse > > > listTags(
	db::DBContext& db,
	http::RequestContext context )
{
	const auto record_id { *context.parameters().scalar< RecordID >( "record_id" ) };

	db::DBBindings bindings {};
	bindings.bind( record_id );
	const auto rows {
		co_await db
			.query( "SELECT tag_domain_id, tag_id FROM tag_mappings WHERE record_id = $1", std::move( bindings ) )
			.rows< TagDomainID, TagID >()
	};

	std::unordered_map< TagDomainID, std::vector< TagID > > domain_map {};
	for ( const auto& [ tag_domain_id, tag_id ] : rows ) domain_map[ tag_domain_id ].push_back( tag_id );

	co_return http::JsonResponse< std::vector< RecordDomainTagsResponse > > {
		recordDomainTagGroups( std::move( domain_map ) )
	};
}

http::HTTPTask< http::JsonResult< std::vector< RecordDomainTagsResponse > > > listActiveTags(
	db::DBContext& db,
	http::RequestContext context )
{
	const auto record_id { *context.parameters().scalar< RecordID >( "record_id" ) };

	db::DBBindings bindings {};
	bindings.bind( record_id );
	const auto rows {
		co_await db
			.query(
				"SELECT DISTINCT tag_id, tag_domain_id FROM active_tag_mappings_final WHERE record_id = $1",
				std::move( bindings ) )
			.rows< TagID, TagDomainID >()
	};

	std::unordered_map< TagDomainID, std::vector< TagID > > domain_map {};
	for ( const auto& [ tag_id, tag_domain_id ] : rows ) domain_map[ tag_domain_id ].push_back( tag_id );

	co_return http::JsonResponse< std::vector< RecordDomainTagsResponse > > {
		recordDomainTagGroups( std::move( domain_map ) )
	};
}

http::HTTPTask< http::JsonResult< std::vector< RecordTagProvenanceResponse > > > listActiveTagsVerbose(
	db::DBContext& db,
	http::RequestContext context )
{
	const auto record_id { *context.parameters().scalar< RecordID >( "record_id" ) };

	db::DBBindings bindings {};
	bindings.bind( record_id );

	static constexpr char query[] {
#embed "sql/active_record_tag_provenance.sql" suffix(, )

		'\0'
	};

	const auto rows { co_await db.query( query, std::move( bindings ) )
	                      .rows< TagID, TagDomainID, std::optional< TagID >, std::optional< TagID > >() };

	struct TagProvenance
	{
		bool is_explicit;
		std::vector< TagID > aliased_from;
		std::vector< TagID > inherited_from;

		TagProvenance() : is_explicit( false ), aliased_from(), inherited_from() {}
	};

	std::unordered_map< TagDomainID, std::unordered_map< TagID, TagProvenance > > provenance_map;

	for ( const auto& [ effective_tag_id, tag_domain_id, original_tag_id, origin_id ] : rows )
	{
		auto& prov { provenance_map[ tag_domain_id ][ effective_tag_id ] };

		if ( origin_id && *origin_id != 0 )
			prov.inherited_from.push_back( *origin_id );
		else if ( original_tag_id && *original_tag_id != 0 )
		{
			if ( *original_tag_id == effective_tag_id )
				prov.is_explicit = true;
			else
				prov.aliased_from.push_back( *original_tag_id );
		}
	}

	std::vector< RecordTagProvenanceResponse > response;
	for ( auto& [ domain_id, tag_map ] : provenance_map )
		for ( auto& [ tag_id, provenance ] : tag_map )
			response.push_back(
				{ tag_id,
				  domain_id,
				  provenance.is_explicit,
				  std::move( provenance.aliased_from ),
				  std::move( provenance.inherited_from ) } );

	co_return http::JsonResponse< std::vector< RecordTagProvenanceResponse > > { std::move( response ) };
}

http::HTTPTask< http::JsonResult< TagUpdateResponse > > addTags(
	db::DBContext& db,
	http::RequestContext context,
	http::Query< AddTagsQuery > query )
{
	const auto record_id { *context.parameters().scalar< RecordID >( "record_id" ) };

	auto body_e { readValidatedJsonBody( context ) };
	if ( !body_e ) co_return std::move( body_e.error() );

	simdjson::dom::parser parser;
	simdjson::dom::array array {};
	try
	{
		if ( parser.parse( body_e->data(), body_e->size() ).get( array ) != simdjson::SUCCESS )
			co_return http::Response::error( 400, "Invalid json tag array: Was not array" );
	}
	catch ( const simdjson::simdjson_error& e )
	{
		co_return http::Response::error( 400, e.what() );
	}

	auto pairs { parseTagPairs( array ) };
	if ( !pairs ) co_return std::move( pairs.error() );

	auto tag_ids { co_await resolveTagPairIds( *pairs, db ) };
	if ( !tag_ids ) co_return std::move( tag_ids.error() );

	auto result { co_await addTagsToRecord( record_id, std::move( *tag_ids ), query.value.tag_domain_id, db ) };
	if ( !result ) co_return std::move( result.error() );

	co_return http::JsonResponse< TagUpdateResponse > {};
}

http::HTTPTask< http::JsonResult< std::variant< TagUpdateResponse, TagUpdateStatusResponse > > > removeTags(
	db::DBContext& db,
	http::RequestContext context,
	http::Query< RemoveTagsQuery > query )
{
	const auto record_id { *context.parameters().scalar< RecordID >( "record_id" ) };

	auto body_e { readValidatedJsonBody( context ) };
	if ( !body_e ) co_return std::move( body_e.error() );

	simdjson::dom::parser parser;
	simdjson::dom::array array {};
	try
	{
		if ( parser.parse( body_e->data(), body_e->size() ).get( array ) != simdjson::SUCCESS )
			co_return http::Response::error( 400, "Invalid json: Expected array of tag_ids" );
	}
	catch ( const simdjson::simdjson_error& e )
	{
		co_return http::Response::error( 400, e.what() );
	}

	std::vector< TagID > tag_ids {};
	tag_ids.reserve( array.size() );
	for ( const simdjson::dom::element item : array )
	{
		auto id { parsePositiveId< TagID >( item, "tag_id" ) };
		if ( !id ) co_return std::move( id.error() );

		tag_ids.push_back( *id );
	}

	if ( tag_ids.empty() )
		co_return http::JsonResponse< std::variant< TagUpdateResponse, TagUpdateStatusResponse > > {
			TagUpdateResponse {}
		};

	try
	{
		co_await removeTagMappings( record_id, std::move( tag_ids ), query.value.tag_domain_id, db );
	}
	catch ( const std::exception& e )
	{
		co_return http::Response::error( 500, std::format( "Error removing tags: {}", e.what() ) );
	}

	co_return http::JsonResponse< std::variant< TagUpdateResponse, TagUpdateStatusResponse > > {
		TagUpdateStatusResponse {}
	};
}

static std::expected< std::vector< TagPair >, http::Response > ownedTagPairs(
	const std::vector< RecordTagInput >& tags )
{
	std::vector< TagPair > pairs;
	for ( const auto& tag : tags )
	{
		TagPair pair;
		if ( std::holds_alternative< TagID >( tag ) )
		{
			const auto tag_id { std::get< TagID >( tag ) };
			if ( tag_id <= 0 ) return std::unexpected( http::Response::error( 400, "Tag IDs must be positive" ) );

			pair.tag_id = tag_id;
		}
		else if ( std::holds_alternative< std::string >( tag ) )
		{
			auto split { splitTag( std::get< std::string >( tag ) ) };
			pair.tag_namespace = std::move( split.first );
			pair.tag_subtag = std::move( split.second );
		}
		else
		{
			const auto& object { std::get< RecordTagObject >( tag ) };
			if ( std::holds_alternative< NamespaceID >( object.tag_namespace ) )
			{
				if ( std::get< NamespaceID >( object.tag_namespace ) <= 0 )
					return std::unexpected( http::Response::error( 400, "Namespace IDs must be positive" ) );
			}
			pair.tag_namespace = object.tag_namespace;
			pair.tag_subtag = object.subtag;
		}
		pairs.push_back( std::move( pair ) );
	}
	return pairs;
}

http::HTTPTask< http::JsonResult< TagUpdateResponse > > addMultipleTags(
	db::DBContext& db,
	http::Query< RecordTagDomainQuery > query,
	http::JsonBody< AddRecordTagsBody > body )
{
	const auto tag_domain_id { query.value.tag_domain_id };
	auto& request { body.value };
	if ( request.records.empty() ) co_return http::Response::error( 400, "records must not be empty" );

	for ( const auto record_id : request.records )
	{
		if ( record_id <= 0 ) co_return http::Response::error( 400, "Record IDs must be positive" );
	}

	if ( request.sets.has_value() )
	{
		if ( request.sets.value().size() != request.records.size() )
			co_return http::Response::error( 400, "Number of sets must match number of records" );
	}

	const auto domain { co_await findTagDomain( tag_domain_id, db ) };
	if ( !domain.has_value() ) co_return http::Response::error( 400, "Invalid tag domain ID" );

	auto validation_e { co_await validateRecordIds( request.records, db ) };
	if ( !validation_e.has_value() ) co_return std::move( validation_e.error() );

	if ( request.tags.has_value() )
	{
		auto pairs_e { ownedTagPairs( request.tags.value() ) };
		if ( !pairs_e.has_value() ) co_return std::move( pairs_e.error() );

		auto tags_e { co_await resolveTagPairIds( *pairs_e, db ) };
		if ( !tags_e.has_value() ) co_return std::move( tags_e.error() );

		auto result_e { co_await addTagsToRecords( request.records, std::move( *tags_e ), tag_domain_id, db ) };
		if ( !result_e.has_value() ) co_return std::move( result_e.error() );
	}

	if ( request.sets.has_value() )
	{
		std::vector< runtime::Task< std::expected< std::vector< TagID >, http::Response > > > tasks;
		for ( const auto& set : request.sets.value() )
		{
			auto pairs_e { ownedTagPairs( set ) };
			if ( !pairs_e.has_value() ) co_return std::move( pairs_e.error() );

			tasks.push_back( resolveOwnedTagPairIds( std::move( *pairs_e ), db ) );
		}
		auto results { co_await folly::coro::collectAllRange( std::move( tasks ) ) };
		std::vector< std::vector< TagID > > sets;
		for ( auto& result_e : results )
		{
			if ( !result_e.has_value() ) co_return std::move( result_e.error() );

			sets.push_back( std::move( *result_e ) );
		}
		auto result_e { co_await addTagSetsToRecords( request.records, sets, tag_domain_id, db ) };
		if ( !result_e.has_value() ) co_return std::move( result_e.error() );
	}
	co_return http::JsonResponse< TagUpdateResponse > {};
}

http::HTTPTask< http::JsonResult< TagUpdateStatusResponse > > removeMultipleTags(
	db::DBContext& db,
	http::RequestContext context,
	http::Query< RemoveMultipleTagsQuery > query )
{
	const auto tag_domain_id { query.value.tag_domain_id };

	auto body_e { readValidatedJsonBody( context ) };
	if ( !body_e ) co_return std::move( body_e.error() );

	simdjson::dom::parser parser;
	simdjson::dom::object root {};
	simdjson::dom::array records_array {}, sets_array {};
	try
	{
		if ( parser.parse( body_e->data(), body_e->size() ).get( root ) != simdjson::SUCCESS )
			co_return http::Response::error( 400, "Invalid json object. Expected object as root item" );
		if ( root[ "records" ].get( records_array ) != simdjson::SUCCESS )
			co_return http::Response::error( 400, "Invalid json: Array of ids called 'records' must be present." );
		if ( root[ "sets" ].get( sets_array ) != simdjson::SUCCESS )
			co_return http::Response::error( 400, "Invalid json: Array of tag sets called 'sets' must be present." );
	}
	catch ( const simdjson::simdjson_error& e )
	{
		co_return http::Response::error( 400, e.what() );
	}

	if ( records_array.size() == 0 ) co_return http::Response::error( 400, "records must not be empty" );

	if ( sets_array.size() != records_array.size() )
		co_return http::Response::error(
			400,
			std::format(
				"Sets vs Records size mismatch. Number of sets must match number of records. Got {} expected {}",
				sets_array.size(),
				records_array.size() ) );

	std::vector< RecordID > record_ids {};
	record_ids.reserve( records_array.size() );
	for ( const simdjson::dom::element item : records_array )
	{
		auto id { parsePositiveId< RecordID >( item, "record_id" ) };
		if ( !id ) co_return std::move( id.error() );

		record_ids.push_back( *id );
	}

	std::vector< std::vector< TagID > > tag_sets {};
	tag_sets.reserve( sets_array.size() );
	for ( const simdjson::dom::element set_item : sets_array )
	{
		simdjson::dom::array set_array {};
		if ( set_item.get_array().get( set_array ) != simdjson::SUCCESS )
			co_return http::Response::error( 400, "Invalid json: Each set must be an array of tag_ids" );

		std::vector< TagID > tag_ids {};
		tag_ids.reserve( set_array.size() );
		for ( const simdjson::dom::element item : set_array )
		{
			auto id { parsePositiveId< TagID >( item, "tag_id" ) };
			if ( !id ) co_return std::move( id.error() );

			tag_ids.push_back( *id );
		}
		tag_sets.push_back( std::move( tag_ids ) );
	}

	try
	{
		std::vector< runtime::Task<> > tasks {};
		tasks.reserve( record_ids.size() );
		for ( std::size_t i = 0; i < record_ids.size(); ++i )
		{
			if ( tag_sets[ i ].empty() ) continue;
			tasks.push_back( removeTagMappings( record_ids[ i ], std::move( tag_sets[ i ] ), tag_domain_id, db ) );
		}
		co_await folly::coro::collectAllRange( std::move( tasks ) );
	}
	catch ( const std::exception& e )
	{
		co_return http::Response::error( 500, std::format( "Error removing tags: {}", e.what() ) );
	}

	co_return http::JsonResponse< TagUpdateStatusResponse > {};
}

http::HTTPTask< http::JsonResult< CreatedRecordResponse > > createRecord(
	db::DBContext& db,
	http::RequestContext context )
{
	auto body_e { readValidatedJsonBody( context ) };
	if ( !body_e ) co_return std::move( body_e.error() );

	simdjson::dom::parser parser;
	simdjson::dom::object root {};
	try
	{
		if ( parser.parse( body_e->data(), body_e->size() ).get( root ) != simdjson::SUCCESS )
			co_return http::Response::error( 400, "Invalid json object. Expected object as root item" );
	}
	catch ( const simdjson::simdjson_error& e )
	{
		co_return http::Response::error( 400, e.what() );
	}

	simdjson::dom::array hash_array {};
	if ( root[ "sha256" ].get( hash_array ) == simdjson::SUCCESS )
	{
		std::vector< SHA256 > hashes_order {};
		std::unordered_set< SHA256 > unique_hashes {};

		for ( const simdjson::dom::element item : hash_array )
		{
			std::string_view text {};
			if ( item.get_string().get( text ) != simdjson::SUCCESS )
				co_return http::Response::error( 400, "Json value in array must be a string" );

			auto hash { parseSha256Hex( text ) };
			if ( !hash ) co_return std::move( hash.error() );

			hashes_order.push_back( *hash );
			unique_hashes.insert( *hash );
		}

		const std::vector< SHA256 > hashes_vec { unique_hashes.begin(), unique_hashes.end() };
		const auto record_ids { co_await massCreateRecord( hashes_vec, db ) };

		std::unordered_map< SHA256, RecordID > record_id_map {};
		for ( std::size_t i = 0; i < hashes_vec.size(); ++i ) record_id_map.emplace( hashes_vec[ i ], record_ids[ i ] );

		if ( hashes_order.size() != record_id_map.size() )
			log::warn(
				"Create records endpoint got {} hashes but only {} were unique, This impacts performance, Only "
				"submit unique hashes",
				hashes_order.size(),
				record_id_map.size() );

		std::vector< RecordID > response;
		response.reserve( hashes_order.size() );
		for ( const auto& hash : hashes_order ) response.push_back( record_id_map.at( hash ) );

		co_return http::JsonResponse< CreatedRecordResponse > { std::move( response ) };
	}

	std::string_view hash_text {};
	if ( root[ "sha256" ].get( hash_text ) == simdjson::SUCCESS )
	{
		auto hash { parseSha256Hex( hash_text ) };
		if ( !hash ) co_return std::move( hash.error() );

		auto record_id { co_await createRecord( *hash, db ) };
		if ( !record_id ) co_return std::move( record_id.error() );

		co_return http::JsonResponse< CreatedRecordResponse > { RecordIdResponse { *record_id } };
	}

	co_return http::Response::error( 400, "Invalid json: 'sha256' must be a hex string or an array of hex strings" );
}

http::HTTPTask< http::JsonResult< RecordHashSearchResponse > > searchHash(
	db::DBContext& db,
	http::Query< SearchHashQuery > query )
{
	auto hash { parseSha256Hex( query.value.sha256 ) };
	if ( !hash ) co_return std::move( hash.error() );

	const auto record_id { co_await findRecord( *hash, db ) };

	co_return http::JsonResponse< RecordHashSearchResponse > {
		{ record_id.has_value(), http::possiblyMissing< RecordID > { record_id } }
	};
}

http::HTTPTask< http::JsonResult< RecordIdResponse > > getRandomActiveRecord(
	db::DBContext& db,
	[[maybe_unused]] http::RequestContext context )
{
	const auto rows {
		co_await db.query( "SELECT record_id FROM file_info WHERE mime_id IS NOT NULL ORDER BY RANDOM() LIMIT 1" )
			.rows< RecordID >()
	};

	if ( rows.empty() ) co_return http::Response::error( 404, "No active records found" );

	co_return http::JsonResponse< RecordIdResponse > { { std::get< 0 >( rows.row( 0 ) ) } };
}

http::HTTPTask< http::JsonResult< RecordFileDeletedResponse > > deleteRecordFile(
	db::DBContext& db,
	http::RequestContext context )
{
	const auto record_id_e { context.parameters().scalar< RecordID >( "record_id" ) };
	if ( !record_id_e.has_value() ) co_return http::Response::error( 400, record_id_e.error().message );
	const RecordID record_id { record_id_e.value() };
	db::DBBindings bindings {};
	bindings.bind( record_id );
	const auto rows {
		co_await db
			.query(
				"SELECT file_info.cluster_id, file_info.cluster_delete_time IS NOT NULL "
				"FROM file_info WHERE file_info.record_id = $1",
				std::move( bindings ) )
			.rows< std::optional< ClusterID >, std::optional< bool > >()
	};

	if ( rows.empty() ) co_return http::Response::error( 404, std::format( "Record {} has no file info", record_id ) );

	const auto [ cluster_id, deletion_recorded ] { rows.row( 0 ) };
	if ( !deletion_recorded.has_value() )
		co_return http::Response::error( 404, std::format( "Record {} has no file info", record_id ) );

	if ( deletion_recorded.value() )
	{
		co_return http::JsonResponse< RecordFileDeletedResponse > { { false } };
	}

	if ( cluster_id.has_value() )
	{
		auto location_e { co_await filesystem::findRecordFilePath( db, record_id ) };
		if ( !location_e.has_value() ) co_return http::Response::error( 503, location_e.error() );

		auto& optional_location { location_e.value() };
		if ( !optional_location.has_value() )
			co_return http::Response::error( 409, "The record no longer has an assigned file cluster" );

		auto& location { optional_location.value() };
		if ( location.cluster().readonly() )
			co_return http::Response::error( 409, "The record's cluster is read-only" );

		const auto verified_e { location.verifySync() };
		if ( !verified_e.has_value() ) co_return http::Response::error( 503, verified_e.error() );

		const auto removed { co_await IOUring::getInstance().removeFile( location.path() ) };
		if ( removed != 0 && removed != -ENOENT )
			co_return http::Response::error( 500, std::format( "Could not delete record file: errno {}", removed ) );
	}

	db::DBBindings update_bindings {};
	update_bindings.bind( record_id );
	co_await db
		.query(
			"UPDATE file_info SET cluster_id = NULL, cluster_delete_time = now() "
			"WHERE record_id = $1 AND cluster_delete_time IS NULL",
			std::move( update_bindings ) )
		.result();
	co_return http::JsonResponse< RecordFileDeletedResponse > { { true } };
}

} // namespace idhan::httpapi
