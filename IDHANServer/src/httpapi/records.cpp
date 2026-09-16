#include "records.hpp"

#include <folly/coro/Collect.h>
#include <folly/io/IOBuf.h>

#include <format>
#include <limits>
#include <simdjson.h>
#include <unordered_map>
#include <unordered_set>

#include "apiAuth.hpp"
#include "decodeHex.hpp"
#include "helpers.hpp"
#include "idhan/SHA256.hpp"
#include "idhan/db/DBContext.hpp"
#include "jsonBody.hpp"
#include "logging/log.hpp"
#include "recordValidator.hpp"
#include "splitTag.hpp"
#include "tagHelpers.hpp"
#include "urls/UrlDomain.hpp"

namespace idhan::httpapi
{

[[nodiscard]] static http::Response jsonResponse(
	simdjson::builder::string_builder& builder,
	const std::uint16_t status = 200 )
{
	auto json { builder.view() };
	if ( json.error() ) throw std::runtime_error( simdjson::error_message( json.error() ) );

	return http::Response::json( status, folly::IOBuf::copyBuffer( *json ) );
}

[[nodiscard]] static http::Response emptyJsonResponse()
{
	return http::Response::json( 200, folly::IOBuf::copyBuffer( std::string_view { "{}" } ) );
}

[[nodiscard]] static std::expected< SHA256, http::Response > parseSha256Hex( const std::string_view text )
{
	constexpr std::size_t hex_length { SHA256::size() * 2 };
	if ( text.size() != hex_length )
		return std::unexpected(
			http::Response::error(
				400, std::format( "A sha256 is {} hex characters, got {}", hex_length, text.size() ) ) );

	try
	{
		return SHA256::fromBuffer( decodeHex( std::string { text } ) );
	}
	catch ( const std::exception& e )
	{
		return std::unexpected( http::Response::error( 400, std::format( "Invalid sha256: {}", e.what() ) ) );
	}
}

runtime::Task< std::optional< RecordID > > findRecord( const SHA256& sha256, db::DBContext& db )
{
	db::DBBindings bindings {};
	bindings.bind( std::vector< std::byte > { sha256.data().begin(), sha256.data().end() } );

	const auto rows {
		co_await db.query( "SELECT record_id FROM records WHERE sha256 = $1", std::move( bindings ) ).rows< RecordID >()
	};

	if ( rows.empty() ) co_return std::nullopt;
	co_return std::get< 0 >( rows.row( 0 ) );
}

runtime::Task< std::expected< RecordID, http::Response > > createRecord( const SHA256& sha256, db::DBContext& db )
{
	if ( const auto existing { co_await findRecord( sha256, db ) }; existing ) co_return *existing;

	constexpr std::size_t max_tries { 16 };

	for ( std::size_t tries { 0 }; tries < max_tries; ++tries )
	{
		db::DBBindings bindings {};
		bindings.bind( std::vector< std::byte > { sha256.data().begin(), sha256.data().end() } );

		const auto rows {
			co_await db
				.query(
					"INSERT INTO records (sha256) VALUES ($1) ON CONFLICT DO NOTHING RETURNING record_id",
					std::move( bindings ) )
				.rows< RecordID >()
		};

		if ( !rows.empty() ) co_return std::get< 0 >( rows.row( 0 ) );

		if ( const auto existing { co_await findRecord( sha256, db ) }; existing ) co_return *existing;
	}

	co_return std::unexpected(
		http::Response::error( 500, std::format( "Failed to insert record {}", sha256.hex() ) ) );
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
		record_id_map.emplace(
			SHA256::fromBuffer( std::vector< std::byte > { sha256_bytes.begin(), sha256_bytes.end() } ), record_id );

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
			pair.tag_namespace = static_cast< NamespaceID >( namespace_int );
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

static runtime::Task< http::Response > recordNotesResponse( const RecordID record_id, db::DBContext& db )
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

	simdjson::builder::string_builder builder;
	builder.start_array();
	bool first { true };
	for ( const auto& [ note, note_id ] : rows )
	{
		if ( !first ) builder.append_comma();
		first = false;
		builder.start_object();
		builder.append_key_value( "note_id", note_id );
		builder.append_comma();
		builder.append_key_value( "text", note );
		builder.end_object();
	}
	builder.end_array();

	co_return jsonResponse( builder );
}

static http::HTTPTask< http::Response > getNotes( db::DBContext& db, http::RequestContext context )
{
	const auto record_id { *context.parameters().scalar< RecordID >( "record_id" ) };
	co_return co_await recordNotesResponse( record_id, db );
}

static http::HTTPTask< http::Response > addNote( db::DBContext& db, http::RequestContext context )
{
	const auto record_id { *context.parameters().scalar< RecordID >( "record_id" ) };

	const auto content_type { context.message().getHeaders().getSingleOrEmpty( "Content-Type" ) };
	if ( !content_type.empty() && !content_type.starts_with( "text/plain" ) )
		co_return http::Response::error( 400, "Content type must be text/plain" );

	auto body { std::move( context ).takeBody() };
	if ( !std::holds_alternative< std::unique_ptr< folly::IOBuf > >( body ) )
		co_return http::Response::error( 400, "Request body is too large" );

	const auto text { std::get< std::unique_ptr< folly::IOBuf > >( body )->to< std::string >() };

	auto note_id { co_await findOrCreateNote( text, db ) };
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

static http::HTTPTask< http::Response > removeNote( db::DBContext& db, http::RequestContext context )
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

static http::HTTPTask< http::Response > fetchUrls( db::DBContext& db, http::RequestContext context )
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

	simdjson::builder::string_builder builder;
	builder.start_array();
	bool first { true };
	for ( const auto& [ url ] : rows )
	{
		if ( !first ) builder.append_comma();
		builder.append( url );
		first = false;
	}
	builder.end_array();

	co_return jsonResponse( builder );
}

static http::HTTPTask< http::Response > addUrls( db::DBContext& db, http::RequestContext context )
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

	if ( urls_array.size() == 0 ) co_return emptyJsonResponse();

	std::vector< std::string > urls {};
	urls.reserve( urls_array.size() );
	for ( const simdjson::dom::element item : urls_array )
	{
		std::string_view url {};
		if ( item.get_string().get( url ) != simdjson::SUCCESS )
			co_return http::Response::error( 400, "Invalid item in urls array: Expected string" );
		urls.emplace_back( url );
	}

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

	db::DBBindings url_bindings {};
	url_bindings.bind( urls );
	url_bindings.bind( domains );
	co_await db
		.query(
			"INSERT INTO urls (url, url_domain_id) "
			"SELECT pairs.url, ud.url_domain_id FROM UNNEST($1, $2) AS pairs(url, domain) "
			"JOIN url_domains ud ON ud.url_domain = pairs.domain ON CONFLICT (url) DO NOTHING",
			std::move( url_bindings ) )
		.result();

	db::DBBindings mapping_bindings {};
	mapping_bindings.bind( urls );
	mapping_bindings.bind( record_id );
	co_await db
		.query(
			"INSERT INTO url_mappings (url_id, record_id) "
			"SELECT url_id, $2 FROM urls WHERE url = ANY($1) ON CONFLICT DO NOTHING",
			std::move( mapping_bindings ) )
		.result();

	co_return emptyJsonResponse();
}

static http::HTTPTask< http::Response > removeUrls( db::DBContext& db, http::RequestContext context )
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

	db::DBBindings bindings {};
	bindings.bind( urls );
	bindings.bind( record_id );
	co_await db
		.query(
			"DELETE FROM url_mappings um USING urls u "
			"WHERE u.url = ANY($1) AND um.url_id = u.url_id AND um.record_id = $2",
			std::move( bindings ) )
		.result();

	co_return http::Response::json( 200, folly::IOBuf::copyBuffer( std::string_view {} ) );
}

static void appendDomainTagGroups(
	simdjson::builder::string_builder& builder,
	const std::unordered_map< TagDomainID, std::vector< TagID > >& domain_map )
{
	builder.start_array();
	bool first { true };
	for ( const auto& [ domain_id, tag_ids ] : domain_map )
	{
		if ( !first ) builder.append_comma();
		first = false;

		builder.start_object();
		builder.append_key_value( "tag_domain_id", domain_id );
		builder.append_comma();
		builder.escape_and_append_with_quotes( "tag_ids" );
		builder.append_colon();
		builder.start_array();
		bool first_id { true };
		for ( const auto tag_id : tag_ids )
		{
			if ( !first_id ) builder.append_comma();
			builder.append( tag_id );
			first_id = false;
		}
		builder.end_array();
		builder.end_object();
	}
	builder.end_array();
}

static http::HTTPTask< http::Response > listTags( db::DBContext& db, http::RequestContext context )
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

	simdjson::builder::string_builder builder;
	appendDomainTagGroups( builder, domain_map );
	co_return jsonResponse( builder );
}

static http::HTTPTask< http::Response > listActiveTags( db::DBContext& db, http::RequestContext context )
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

	simdjson::builder::string_builder builder;
	appendDomainTagGroups( builder, domain_map );
	co_return jsonResponse( builder );
}

static http::HTTPTask< http::Response > listActiveTagsVerbose( db::DBContext& db, http::RequestContext context )
{
	const auto record_id { *context.parameters().scalar< RecordID >( "record_id" ) };

	db::DBBindings bindings {};
	bindings.bind( record_id );

	const auto rows {
		co_await db
			.query(
				R"(
				SELECT COALESCE(ideal_tag_id, tag_id) AS effective_tag_id, tag_domain_id,
				       tag_id AS original_tag_id, NULL::INTEGER AS origin_id
				FROM active_tag_mappings
				WHERE record_id = $1
				UNION ALL
				SELECT tag_id, tag_domain_id,
				       NULL::INTEGER AS original_tag_id, origin_id
				FROM active_tag_mappings_parents
				WHERE record_id = $1
			)",
				std::move( bindings ) )
			.rows< TagID, TagDomainID, std::optional< TagID >, std::optional< TagID > >()
	};

	struct TagProvenance
	{
		bool is_explicit { false };
		std::vector< TagID > aliased_from;
		std::vector< TagID > inherited_from;
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

	simdjson::builder::string_builder builder;
	builder.start_array();
	bool first { true };
	for ( const auto& [ domain_id, tag_map ] : provenance_map )
		for ( const auto& [ tag_id, prov ] : tag_map )
		{
			if ( !first ) builder.append_comma();
			first = false;

			builder.start_object();
			builder.append_key_value( "tag_id", tag_id );
			builder.append_comma();
			builder.append_key_value( "tag_domain_id", domain_id );
			builder.append_comma();
			builder.append_key_value( "explicit", prov.is_explicit );
			builder.append_comma();
			builder.escape_and_append_with_quotes( "aliased_from" );
			builder.append_colon();
			builder.start_array();
			bool first_a { true };
			for ( const auto id : prov.aliased_from )
			{
				if ( !first_a ) builder.append_comma();
				builder.append( id );
				first_a = false;
			}
			builder.end_array();
			builder.append_comma();
			builder.escape_and_append_with_quotes( "inherited_from" );
			builder.append_colon();
			builder.start_array();
			bool first_i { true };
			for ( const auto id : prov.inherited_from )
			{
				if ( !first_i ) builder.append_comma();
				builder.append( id );
				first_i = false;
			}
			builder.end_array();
			builder.end_object();
		}
	builder.end_array();

	co_return jsonResponse( builder );
}

static http::HTTPTask< http::Response > addTags( db::DBContext& db, http::RequestContext context )
{
	const auto record_id { *context.parameters().scalar< RecordID >( "record_id" ) };

	auto tag_domain_id_e { tagDomainIdParameter( context.parameters() ) };
	if ( !tag_domain_id_e ) co_return std::move( tag_domain_id_e.error() );

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

	auto result { co_await addTagsToRecord( record_id, std::move( *tag_ids ), *tag_domain_id_e, db ) };
	if ( !result ) co_return std::move( result.error() );

	co_return emptyJsonResponse();
}

static http::HTTPTask< http::Response > removeTags( db::DBContext& db, http::RequestContext context )
{
	const auto record_id { *context.parameters().scalar< RecordID >( "record_id" ) };

	auto tag_domain_id_e { tagDomainIdParameter( context.parameters() ) };
	if ( !tag_domain_id_e ) co_return std::move( tag_domain_id_e.error() );

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
		std::int64_t id {};
		if ( item.get_int64().get( id ) != simdjson::SUCCESS )
			co_return http::Response::error( 400, "Invalid tag_id in array: Must be integral" );
		tag_ids.push_back( static_cast< TagID >( id ) );
	}

	if ( tag_ids.empty() ) co_return emptyJsonResponse();

	try
	{
		co_await removeTagMappings( record_id, std::move( tag_ids ), *tag_domain_id_e, db );
	}
	catch ( const std::exception& e )
	{
		co_return http::Response::error( 500, std::format( "Error removing tags: {}", e.what() ) );
	}

	simdjson::builder::string_builder builder;
	builder.start_object();
	builder.append_key_value( "status", 200 );
	builder.end_object();
	co_return jsonResponse( builder );
}

static http::HTTPTask< http::Response > addMultipleTags( db::DBContext& db, http::RequestContext context )
{
	auto tag_domain_id_e { tagDomainIdParameter( context.parameters() ) };
	if ( !tag_domain_id_e ) co_return std::move( tag_domain_id_e.error() );
	const auto tag_domain_id { *tag_domain_id_e };

	auto body_e { readValidatedJsonBody( context ) };
	if ( !body_e ) co_return std::move( body_e.error() );

	simdjson::dom::parser parser;
	simdjson::dom::object root {};
	simdjson::dom::array records_array {}, tags_array {}, sets_array {};
	bool has_tags { false }, has_sets { false };
	try
	{
		if ( parser.parse( body_e->data(), body_e->size() ).get( root ) != simdjson::SUCCESS )
			co_return http::Response::error( 400, "Invalid json object. Expected object as root item" );

		if ( root[ "records" ].get( records_array ) != simdjson::SUCCESS )
			co_return http::Response::error( 400, "Invalid json: Array of ids called 'records' must be present." );

		has_tags = root[ "tags" ].get( tags_array ) == simdjson::SUCCESS;
		has_sets = root[ "sets" ].get( sets_array ) == simdjson::SUCCESS;
	}
	catch ( const simdjson::simdjson_error& e )
	{
		co_return http::Response::error( 400, e.what() );
	}

	if ( tag_domain_id <= 0 )
		co_return http::Response::error(
			400, std::format( "Invalid domain ID given: Expected tag_domain_id > 0; Got {}", tag_domain_id ) );

	if ( const auto domain { co_await findTagDomain( tag_domain_id, db ) }; !domain )
		co_return http::Response::error(
			400, std::format( "Invalid domain ID given: Got no IDs (searched for {})", tag_domain_id ) );

	std::vector< RecordID > record_ids {};
	record_ids.reserve( records_array.size() );
	for ( const simdjson::dom::element item : records_array )
	{
		std::int64_t id {};
		if ( item.get_int64().get( id ) != simdjson::SUCCESS )
			co_return http::Response::error( 400, "Invalid json item in records list: Expected integral" );
		record_ids.push_back( static_cast< RecordID >( id ) );
	}

	auto record_validation { co_await validateRecordIds( record_ids, db ) };
	if ( !record_validation ) co_return std::move( record_validation.error() );

	if ( has_tags )
	{
		auto pairs { parseTagPairs( tags_array ) };
		if ( !pairs ) co_return std::move( pairs.error() );

		auto tag_ids { co_await resolveTagPairIds( *pairs, db ) };
		if ( !tag_ids ) co_return std::move( tag_ids.error() );

		auto result { co_await addTagsToRecords( record_ids, *tag_ids, tag_domain_id, db ) };
		if ( !result ) co_return std::move( result.error() );
	}

	if ( has_sets )
	{
		if ( sets_array.size() != records_array.size() )
			co_return http::Response::error(
				400,
				std::format(
					"Sets vs Records size mismatch. Number of sets must match number of records. Got {} expected {}",
					sets_array.size(),
					records_array.size() ) );

		std::vector< runtime::Task< std::expected< std::vector< TagID >, http::Response > > > tasks {};
		tasks.reserve( sets_array.size() );

		for ( const simdjson::dom::element set_item : sets_array )
		{
			simdjson::dom::array set_array {};
			if ( set_item.get_array().get( set_array ) != simdjson::SUCCESS )
				co_return http::Response::error( 400, "Invalid json: Each set must be an array of tag_ids" );

			auto pairs { parseTagPairs( set_array ) };
			if ( !pairs ) co_return std::move( pairs.error() );

			tasks.push_back( resolveOwnedTagPairIds( std::move( *pairs ), db ) );
		}

		auto sets { co_await folly::coro::collectAllRange( std::move( tasks ) ) };

		std::vector< std::vector< TagID > > tag_sets {};
		tag_sets.reserve( sets.size() );
		for ( auto& set_result : sets )
		{
			if ( !set_result ) co_return std::move( set_result.error() );
			tag_sets.push_back( std::move( *set_result ) );
		}

		auto result { co_await addTagSetsToRecords( record_ids, tag_sets, tag_domain_id, db ) };
		if ( !result ) co_return std::move( result.error() );
	}

	co_return emptyJsonResponse();
}

static http::HTTPTask< http::Response > removeMultipleTags( db::DBContext& db, http::RequestContext context )
{
	auto tag_domain_id_e { tagDomainIdParameter( context.parameters() ) };
	if ( !tag_domain_id_e ) co_return std::move( tag_domain_id_e.error() );
	const auto tag_domain_id { *tag_domain_id_e };

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
		std::int64_t id {};
		if ( item.get_int64().get( id ) != simdjson::SUCCESS )
			co_return http::Response::error( 400, "Invalid json item in records list: Expected integral" );
		record_ids.push_back( static_cast< RecordID >( id ) );
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
			std::int64_t id {};
			if ( item.get_int64().get( id ) != simdjson::SUCCESS )
				co_return http::Response::error( 400, "Invalid tag_id in set: Must be integral" );
			tag_ids.push_back( static_cast< TagID >( id ) );
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

	simdjson::builder::string_builder builder;
	builder.start_object();
	builder.append_key_value( "status", 200 );
	builder.end_object();
	co_return jsonResponse( builder );
}

static http::HTTPTask< http::Response > createRecord( db::DBContext& db, http::RequestContext context )
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

		simdjson::builder::string_builder builder;
		builder.start_array();
		bool first { true };
		for ( const auto& hash : hashes_order )
		{
			if ( !first ) builder.append_comma();
			builder.append( record_id_map[ hash ] );
			first = false;
		}
		builder.end_array();
		co_return jsonResponse( builder );
	}

	std::string_view hash_text {};
	if ( root[ "sha256" ].get( hash_text ) == simdjson::SUCCESS )
	{
		auto hash { parseSha256Hex( hash_text ) };
		if ( !hash ) co_return std::move( hash.error() );

		auto record_id { co_await createRecord( *hash, db ) };
		if ( !record_id ) co_return std::move( record_id.error() );

		simdjson::builder::string_builder builder;
		builder.start_object();
		builder.append_key_value( "record_id", *record_id );
		builder.end_object();
		co_return jsonResponse( builder );
	}

	co_return http::Response::error( 400, "Invalid json: 'sha256' must be a hex string or an array of hex strings" );
}

static http::HTTPTask< http::Response > searchHash( db::DBContext& db, http::RequestContext context )
{
	const auto hash_e { context.parameters().scalar< std::string >( "sha256" ) };
	if ( !hash_e ) co_return http::Response::error( 400, "sha256 was not provided in query" );

	auto hash { parseSha256Hex( *hash_e ) };
	if ( !hash ) co_return std::move( hash.error() );

	const auto record_id { co_await findRecord( *hash, db ) };

	simdjson::builder::string_builder builder;
	builder.start_object();
	if ( !record_id )
		builder.append_key_value( "found", false );
	else
	{
		builder.append_key_value( "found", true );
		builder.append_comma();
		builder.append_key_value( "record_id", *record_id );
	}
	builder.end_object();

	co_return jsonResponse( builder );
}

static http::HTTPTask< http::Response > getRandomActiveRecord(
	db::DBContext& db,
	[[maybe_unused]] http::RequestContext context )
{
	const auto rows {
		co_await db.query( "SELECT record_id FROM file_info WHERE mime_id IS NOT NULL ORDER BY RANDOM() LIMIT 1" )
			.rows< RecordID >()
	};

	if ( rows.empty() ) co_return http::Response::error( 404, "No active records found" );

	simdjson::builder::string_builder builder;
	builder.start_object();
	builder.append_key_value( "record_id", std::get< 0 >( rows.row( 0 ) ) );
	builder.end_object();

	co_return jsonResponse( builder );
}

std::vector< http::RouteDefinition > recordRoutes( db::DBContext& db )
{
	std::vector< http::RouteDefinition > routes {};

	const auto add =
		[ &routes, &db ](
			const proxygen::HTTPMethod method,
			std::string path,
			std::vector< http::PathParameter > path_parameters,
			const bool needs_record,
			http::HTTPTask< http::Response > ( *handler )( db::DBContext&, http::RequestContext ) )
	{
		std::vector< http::RequestValidation > before_body { apiAuthValidation( db ) };
		if ( needs_record ) before_body.push_back( recordExistsValidation( db ) );

		auto bound = [ &db, handler ]( http::RequestContext context ) -> http::HTTPTask< http::Response >
		{ co_return co_await handler( db, std::move( context ) ); };

		routes.push_back(
			http::RouteDefinition {
				http::RouteMetadata {
					method, std::move( path ), std::move( path_parameters ), std::move( before_body ) },
				std::move( bound ) } );
	};

	add( proxygen::HTTPMethod::GET, "/records/{record_id}/notes", { recordIdPathParameter() }, true, getNotes );
	add( proxygen::HTTPMethod::POST, "/records/{record_id}/add_note", { recordIdPathParameter() }, true, addNote );
	add( proxygen::HTTPMethod::DELETE,
	     "/records/{record_id}/remove_note/{note_id}",
	     { recordIdPathParameter(), noteIdPathParameter() },
	     true,
	     removeNote );

	add( proxygen::HTTPMethod::POST, "/records/create", {}, false, createRecord );

	add( proxygen::HTTPMethod::GET, "/records/{record_id}/urls", { recordIdPathParameter() }, true, fetchUrls );
	add( proxygen::HTTPMethod::POST, "/records/{record_id}/urls/add", { recordIdPathParameter() }, true, addUrls );
	add(
		proxygen::HTTPMethod::POST, "/records/{record_id}/urls/remove", { recordIdPathParameter() }, true, removeUrls );

	add( proxygen::HTTPMethod::POST, "/records/tags/add", {}, false, addMultipleTags );
	add( proxygen::HTTPMethod::POST, "/records/tags/remove", {}, false, removeMultipleTags );

	add( proxygen::HTTPMethod::POST, "/records/{record_id}/tags/add", { recordIdPathParameter() }, true, addTags );
	add(
		proxygen::HTTPMethod::POST, "/records/{record_id}/tags/remove", { recordIdPathParameter() }, true, removeTags );
	add( proxygen::HTTPMethod::GET, "/records/{record_id}/tags", { recordIdPathParameter() }, true, listTags );
	add( proxygen::HTTPMethod::GET,
	     "/records/{record_id}/tags/active",
	     { recordIdPathParameter() },
	     true,
	     listActiveTags );
	add( proxygen::HTTPMethod::GET,
	     "/records/{record_id}/tags/active/verbose",
	     { recordIdPathParameter() },
	     true,
	     listActiveTagsVerbose );

	add( proxygen::HTTPMethod::GET, "/records/search", {}, false, searchHash );
	add( proxygen::HTTPMethod::GET, "/records/random", {}, false, getRandomActiveRecord );

	return routes;
}

} // namespace idhan::httpapi
