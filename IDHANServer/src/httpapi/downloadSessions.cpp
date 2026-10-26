#include "downloadSessions.hpp"

#include <folly/io/IOBuf.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <format>
#include <libpq-fe.h>
#include <optional>
#include <simdjson.h>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "IDHANTypes.hpp"
#include "apiAuth.hpp"
#include "downloader/DownloadSessionEventHub.hpp"
#include "downloader/DownloadSessionManager.hpp"
#include "idhan/db/DBContext.hpp"
#include "jsonBody.hpp"

namespace idhan::httpapi
{

constexpr std::size_t max_name_length { 200 };
constexpr std::size_t max_url_length { 8192 };

[[nodiscard]] static http::PathParameter downloadSessionIdPathParameter()
{
	return http::PathParameter {
		"session_id",
		[]( const std::string_view value ) -> std::expected< void, http::ParseError >
		{
			DownloadSessionID id {};
			const auto result { std::from_chars( value.data(), value.data() + value.size(), id ) };
			if ( result.ec != std::errc {} || result.ptr != value.data() + value.size() || id <= 0 )
				return std::unexpected( http::ParseError { "session_id", "must be a positive download session id" } );
			return {};
		}
	};
}

[[nodiscard]] static http::PathParameter downloadSessionUrlIdPathParameter()
{
	return http::PathParameter {
		"url_id",
		[]( const std::string_view value ) -> std::expected< void, http::ParseError >
		{
			DownloadSessionUrlID id {};
			const auto result { std::from_chars( value.data(), value.data() + value.size(), id ) };
			if ( result.ec != std::errc {} || result.ptr != value.data() + value.size() || id <= 0 )
				return std::unexpected( http::ParseError { "url_id", "must be a positive download session url id" } );
			return {};
		}
	};
}

[[nodiscard]] static http::Response jsonResponse(
	simdjson::builder::string_builder& builder,
	const std::uint16_t status = 200 )
{
	auto json { builder.view() };
	if ( json.error() ) throw std::runtime_error( simdjson::error_message( json.error() ) );

	return http::Response::json( status, folly::IOBuf::copyBuffer( *json ) );
}

static http::HTTPTask< bool > touchSession( db::DBContext& db, const DownloadSessionID session_id )
{
	db::DBBindings bindings {};
	bindings.bind( session_id );
	const auto result {
		co_await db
			.query(
				"UPDATE download_sessions SET last_used_at = now() WHERE download_session_id = $1",
				std::move( bindings ) )
			.result()
	};
	co_return std::atoll( PQcmdTuples( result.get() ) ) > 0;
}

static void appendSessionFields(
	simdjson::builder::string_builder& builder,
	const DownloadSessionID id,
	const std::string& name,
	const std::int64_t created_at,
	const std::int64_t last_used_at,
	const std::int64_t error_count )
{
	builder.append_key_value( "id", id );
	builder.append_comma();
	builder.append_key_value( "name", name );
	builder.append_comma();
	builder.append_key_value( "created_at", created_at );
	builder.append_comma();
	builder.append_key_value( "last_used_at", last_used_at );
	builder.append_comma();
	builder.append_key_value( "error_count", error_count );
}

struct UrlRow
{
	DownloadSessionUrlID id {};
	std::optional< DownloadSessionUrlID > parent_id {};
	std::string url {};
	std::string state {};
	std::int64_t created_at {};
	std::optional< std::int64_t > finished_at {};
	std::optional< std::string > error {};
	std::optional< std::string > note {};
	std::optional< RecordID > record_id {};
	std::optional< std::int64_t > imported_at {};
};

static void appendUrlFields( simdjson::builder::string_builder& builder, const UrlRow& row )
{
	builder.append_key_value( "id", row.id );
	builder.append_comma();
	builder.escape_and_append_with_quotes( "parent_id" );
	builder.append_colon();
	if ( row.parent_id )
		builder.append( *row.parent_id );
	else
		builder.append_raw( "null" );
	builder.append_comma();
	builder.append_key_value( "url", row.url );
	builder.append_comma();
	builder.append_key_value( "state", row.state );
	builder.append_comma();
	builder.append_key_value( "created_at", row.created_at );
	builder.append_comma();
	builder.escape_and_append_with_quotes( "finished_at" );
	builder.append_colon();
	if ( row.finished_at )
		builder.append( *row.finished_at );
	else
		builder.append_raw( "null" );
	builder.append_comma();
	builder.escape_and_append_with_quotes( "error" );
	builder.append_colon();
	if ( row.error )
		builder.append( *row.error );
	else
		builder.append_raw( "null" );
	builder.append_comma();
	builder.escape_and_append_with_quotes( "note" );
	builder.append_colon();
	if ( row.note )
		builder.append( *row.note );
	else
		builder.append_raw( "null" );
	builder.append_comma();
	builder.escape_and_append_with_quotes( "record_id" );
	builder.append_colon();
	if ( row.record_id )
		builder.append( *row.record_id );
	else
		builder.append_raw( "null" );
}

struct ImportedRecord
{
	RecordID record_id {};
	std::int64_t imported_at {};
	DownloadSessionUrlID url_id {};
};

using UrlNodes = std::unordered_map< DownloadSessionUrlID, UrlRow >;
using UrlChildren = std::unordered_map< DownloadSessionUrlID, std::vector< DownloadSessionUrlID > >;

static void appendUrlNode(
	simdjson::builder::string_builder& builder,
	const DownloadSessionUrlID id,
	const UrlNodes& nodes,
	const UrlChildren& children )
{
	builder.start_object();
	appendUrlFields( builder, nodes.at( id ) );
	builder.append_comma();
	builder.escape_and_append_with_quotes( "children" );
	builder.append_colon();
	builder.start_array();

	const auto found { children.find( id ) };
	if ( found != children.end() )
	{
		bool first { true };
		for ( const auto child : found->second )
		{
			if ( !first ) builder.append_comma();
			first = false;
			appendUrlNode( builder, child, nodes, children );
		}
	}

	builder.end_array();
	builder.end_object();
}

static void collectDescendants(
	const DownloadSessionUrlID id,
	const UrlNodes& nodes,
	const UrlChildren& children,
	std::vector< DownloadSessionUrlID >& order,
	std::unordered_map< RecordID, ImportedRecord >& records )
{
	const auto found { children.find( id ) };
	if ( found == children.end() ) return;

	for ( const auto child : found->second )
	{
		order.emplace_back( child );

		const auto& row { nodes.at( child ) };
		if ( row.record_id && row.imported_at )
			records.try_emplace( *row.record_id, ImportedRecord { *row.record_id, *row.imported_at, child } );

		collectDescendants( child, nodes, children, order, records );
	}
}

[[nodiscard]] static std::expected< std::string, http::Response > parseRequiredStringField(
	const std::string& text,
	const std::string_view field,
	const std::size_t max_length )
{
	simdjson::dom::parser parser;
	try
	{
		const simdjson::dom::element root { parser.parse( text ) };
		if ( !root.is_object() )
			return std::unexpected( http::Response::error( 400, "Request body must be a JSON object" ) );

		std::string_view value_sv {};
		if ( root[ field ].get_string().get( value_sv ) != simdjson::SUCCESS )
			return std::unexpected(
				http::Response::error( 400, std::format( "Request body must contain a string '{}'", field ) ) );

		if ( value_sv.empty() || value_sv.size() > max_length )
			return std::unexpected(
				http::Response::error( 400, std::format( "'{}' must be 1 to {} characters", field, max_length ) ) );

		return std::string { value_sv };
	}
	catch ( const simdjson::simdjson_error& e )
	{
		return std::unexpected( http::Response::error( 400, e.what() ) );
	}
}

[[nodiscard]] static std::expected< std::optional< std::string >, http::Response > parseOptionalStringField(
	const std::string& text,
	const std::string_view field )
{
	simdjson::dom::parser parser;
	try
	{
		const simdjson::dom::element root { parser.parse( text ) };
		if ( !root.is_object() )
			return std::unexpected( http::Response::error( 400, "Request body must be a JSON object" ) );

		std::string_view value_sv {};
		if ( root[ field ].get_string().get( value_sv ) != simdjson::SUCCESS ) return std::optional< std::string > {};

		return std::optional< std::string > { std::string { value_sv } };
	}
	catch ( const simdjson::simdjson_error& e )
	{
		return std::unexpected( http::Response::error( 400, e.what() ) );
	}
}

static http::HTTPTask< std::string > freeSessionName( db::DBContext& db, const std::string& url )
{
	const std::string base { url.size() > 180 ? url.substr( 0, 180 ) : url };

	for ( int suffix { 1 }; suffix < 1000; ++suffix )
	{
		const std::string candidate { suffix == 1 ? base : std::format( "{} ({})", base, suffix ) };
		db::DBBindings bindings {};
		bindings.bind( candidate );
		const auto taken {
			co_await db.query( "SELECT 1 FROM download_sessions WHERE lower(name) = lower($1)", std::move( bindings ) )
				.rows< std::int32_t >()
		};
		if ( taken.empty() ) co_return candidate;
	}

	co_return std::format( "{} ({})", base, std::chrono::system_clock::now().time_since_epoch().count() );
}

[[nodiscard]] static bool parseStatusFilter(
	const std::string_view text,
	std::vector< Int >& output,
	bool& without_status )
{
	for ( std::size_t start { 0 }; start < text.size(); )
	{
		const auto separator { text.find( ',', start ) };
		const auto field { text.substr( start, separator - start ) };
		start = separator == std::string_view::npos ? text.size() : separator + 1;

		if ( field == "none" )
		{
			without_status = true;
			continue;
		}

		Int status {};
		const auto result { std::from_chars( field.data(), field.data() + field.size(), status ) };

		if ( result.ec != std::errc {} || result.ptr != field.data() + field.size() || status < 100 || status > 599 )
			return false;

		output.emplace_back( status );
	}

	return true;
}

static http::HTTPTask< http::Response > listSessions( db::DBContext& db, [[maybe_unused]] http::RequestContext context )
{
	const auto rows {
		co_await db
			.query(
				"SELECT download_session_id, name, "
				"extract(epoch FROM created_at)::bigint AS created_at, "
				"extract(epoch FROM last_used_at)::bigint AS last_used_at, "
				"(SELECT count(*) FROM download_session_errors "
				"WHERE download_session_errors.download_session_id = download_sessions.download_session_id) AS error_count "
				"FROM download_sessions ORDER BY last_used_at DESC, download_session_id DESC" )
			.rows< DownloadSessionID, std::string, std::int64_t, std::int64_t, std::int64_t >()
	};

	simdjson::builder::string_builder builder;
	builder.start_array();

	bool first { true };
	for ( const auto& [ id, name, created_at, last_used_at, error_count ] : rows )
	{
		if ( !first ) builder.append_comma();
		first = false;
		builder.start_object();
		appendSessionFields( builder, id, name, created_at, last_used_at, error_count );
		builder.end_object();
	}

	builder.end_array();

	co_return jsonResponse( builder );
}

static http::HTTPTask< http::Response > createSession( db::DBContext& db, http::RequestContext context )
{
	auto body_e { readValidatedJsonBody( context ) };
	if ( !body_e ) co_return std::move( body_e.error() );

	auto name_e { parseRequiredStringField( *body_e, "name", max_name_length ) };
	if ( !name_e ) co_return std::move( name_e.error() );
	const auto& name { *name_e };

	db::DBBindings existing_bindings {};
	existing_bindings.bind( name );
	const auto existing {
		co_await db
			.query( "SELECT 1 FROM download_sessions WHERE lower(name) = lower($1)", std::move( existing_bindings ) )
			.rows< std::int32_t >()
	};

	if ( !existing.empty() )
		co_return http::Response::error( 409, std::format( "A download session named '{}' already exists", name ) );

	db::DBBindings insert_bindings {};
	insert_bindings.bind( name );
	const auto created {
		co_await db
			.query(
				"INSERT INTO download_sessions (name) VALUES ($1) "
				"RETURNING download_session_id, name, "
				"extract(epoch FROM created_at)::bigint AS created_at, "
				"extract(epoch FROM last_used_at)::bigint AS last_used_at, 0::bigint AS error_count",
				std::move( insert_bindings ) )
			.rows< DownloadSessionID, std::string, std::int64_t, std::int64_t, std::int64_t >()
	};

	const auto& [ id, created_name, created_at, last_used_at, error_count ] { created.row( 0 ) };

	simdjson::builder::string_builder builder;
	builder.start_object();
	appendSessionFields( builder, id, created_name, created_at, last_used_at, error_count );
	builder.end_object();

	co_return jsonResponse( builder, 201 );
}

static http::HTTPTask< http::Response > getSession( db::DBContext& db, http::RequestContext context )
{
	const auto session_id_e { context.parameters().scalar< DownloadSessionID >( "session_id" ) };
	if ( !session_id_e ) co_return http::Response::error( 400, session_id_e.error().message );
	const auto session_id { *session_id_e };

	if ( !( co_await touchSession( db, session_id ) ) )
		co_return http::Response::error( 404, std::format( "No download session with id {}", session_id ) );

	db::DBBindings bindings {};
	bindings.bind( session_id );
	const auto rows {
		co_await db
			.query(
				"SELECT download_session_id, name, "
				"extract(epoch FROM created_at)::bigint AS created_at, "
				"extract(epoch FROM last_used_at)::bigint AS last_used_at, "
				"(SELECT count(*) FROM download_session_errors "
				"WHERE download_session_errors.download_session_id = download_sessions.download_session_id) AS error_count "
				"FROM download_sessions WHERE download_session_id = $1",
				std::move( bindings ) )
			.rows< DownloadSessionID, std::string, std::int64_t, std::int64_t, std::int64_t >()
	};

	const auto& [ id, name, created_at, last_used_at, error_count ] { rows.row( 0 ) };

	simdjson::builder::string_builder builder;
	builder.start_object();
	appendSessionFields( builder, id, name, created_at, last_used_at, error_count );
	builder.end_object();

	co_return jsonResponse( builder );
}

static http::HTTPTask< http::Response > destroySession( db::DBContext& db, http::RequestContext context )
{
	const auto session_id_e { context.parameters().scalar< DownloadSessionID >( "session_id" ) };
	if ( !session_id_e ) co_return http::Response::error( 400, session_id_e.error().message );
	const auto session_id { *session_id_e };

	if ( !( co_await touchSession( db, session_id ) ) )
		co_return http::Response::error( 404, std::format( "No download session with id {}", session_id ) );

	downloader::downloadSessionManager().destroy( session_id );

	db::DBBindings bindings {};
	bindings.bind( session_id );
	const auto deleted {
		co_await db.query( "DELETE FROM download_sessions WHERE download_session_id = $1", std::move( bindings ) )
			.result()
	};

	if ( std::atoll( PQcmdTuples( deleted.get() ) ) == 0 )
		co_return http::Response::error( 404, std::format( "No download session with id {}", session_id ) );

	downloader::DownloadSessionEventHub::instance().notifyRemoved( session_id );

	co_return http::Response::json( 200, folly::IOBuf::copyBuffer( std::string_view { "{\"deleted\":true}" } ) );
}

static http::HTTPTask< http::Response > submitUrl( db::DBContext& db, http::RequestContext context )
{
	const auto session_id_e { context.parameters().scalar< DownloadSessionID >( "session_id" ) };
	if ( !session_id_e ) co_return http::Response::error( 400, session_id_e.error().message );
	const auto session_id { *session_id_e };

	auto body_e { readValidatedJsonBody( context ) };
	if ( !body_e ) co_return std::move( body_e.error() );

	auto url_e { parseRequiredStringField( *body_e, "url", max_url_length ) };
	if ( !url_e ) co_return std::move( url_e.error() );
	const auto& url { *url_e };

	if ( !( co_await touchSession( db, session_id ) ) )
		co_return http::Response::error( 404, std::format( "No download session with id {}", session_id ) );

	db::DBBindings insert_bindings {};
	insert_bindings.bind( session_id );
	insert_bindings.bind( url );
	const auto inserted {
		co_await db
			.query(
				"INSERT INTO download_session_urls (download_session_id, url) VALUES ($1, $2) "
				"RETURNING download_session_url_id, state",
				std::move( insert_bindings ) )
			.rows< DownloadSessionUrlID, std::string >()
	};

	const auto& [ job_id, state ] { inserted.row( 0 ) };

	const auto scheduled { downloader::downloadSessionManager().submit( job_id, session_id, url ) };

	if ( !scheduled )
	{
		db::DBBindings delete_bindings {};
		delete_bindings.bind( job_id );
		co_await db
			.query(
				"DELETE FROM download_session_urls WHERE download_session_url_id = $1", std::move( delete_bindings ) )
			.result();
		co_return http::Response::error( 400, std::format( "Unable to schedule URL: {}", scheduled.error() ) );
	}

	downloader::DownloadSessionEventHub::instance().notify( session_id );

	simdjson::builder::string_builder builder;
	builder.start_object();
	builder.append_key_value( "id", job_id );
	builder.append_comma();
	builder.append_key_value( "url", url );
	builder.append_comma();
	builder.append_key_value( "state", state );
	builder.end_object();

	co_return jsonResponse( builder, 201 );
}

static http::HTTPTask< http::Response > submitUrlSession( db::DBContext& db, http::RequestContext context )
{
	auto body_e { readValidatedJsonBody( context ) };
	if ( !body_e ) co_return std::move( body_e.error() );

	auto url_e { parseRequiredStringField( *body_e, "url", max_url_length ) };
	if ( !url_e ) co_return std::move( url_e.error() );
	const auto& url { *url_e };

	auto name_field_e { parseOptionalStringField( *body_e, "name" ) };
	if ( !name_field_e ) co_return std::move( name_field_e.error() );

	const auto name { name_field_e->has_value() ? **name_field_e : co_await freeSessionName( db, url ) };

	if ( name.empty() || name.size() > max_name_length )
		co_return http::Response::error( 400, "Session name must be 1 to 200 characters" );

	db::DBBindings existing_bindings {};
	existing_bindings.bind( name );
	const auto existing {
		co_await db
			.query( "SELECT 1 FROM download_sessions WHERE lower(name) = lower($1)", std::move( existing_bindings ) )
			.rows< std::int32_t >()
	};

	if ( !existing.empty() )
		co_return http::Response::error( 409, std::format( "A download session named '{}' already exists", name ) );

	db::DBBindings insert_bindings {};
	insert_bindings.bind( name );
	const auto created {
		co_await db
			.query(
				"INSERT INTO download_sessions (name) VALUES ($1) "
				"RETURNING download_session_id, name, "
				"extract(epoch FROM created_at)::bigint AS created_at, "
				"extract(epoch FROM last_used_at)::bigint AS last_used_at, 0::bigint AS error_count",
				std::move( insert_bindings ) )
			.rows< DownloadSessionID, std::string, std::int64_t, std::int64_t, std::int64_t >()
	};

	const auto& [ session_id, created_name, created_at, last_used_at, error_count ] { created.row( 0 ) };

	db::DBBindings url_bindings {};
	url_bindings.bind( session_id );
	url_bindings.bind( url );
	const auto inserted {
		co_await db
			.query(
				"INSERT INTO download_session_urls (download_session_id, url) VALUES ($1, $2) "
				"RETURNING download_session_url_id, parent_url_id, url, state, error, note, record_id, "
				"extract(epoch FROM created_at)::bigint AS created_at, "
				"extract(epoch FROM finished_at)::bigint AS finished_at",
				std::move( url_bindings ) )
			.rows<
				DownloadSessionUrlID,
				std::optional< DownloadSessionUrlID >,
				std::string,
				std::string,
				std::optional< std::string >,
				std::optional< std::string >,
				std::optional< RecordID >,
				std::int64_t,
				std::optional< std::int64_t > >()
	};

	const auto& [ job_id, parent_id, inserted_url, state, error, note, record_id, url_created_at, finished_at ] {
		inserted.row( 0 )
	};

	const auto scheduled { downloader::downloadSessionManager().submit( job_id, session_id, url ) };

	if ( !scheduled )
	{
		db::DBBindings delete_bindings {};
		delete_bindings.bind( session_id );
		co_await db
			.query( "DELETE FROM download_sessions WHERE download_session_id = $1", std::move( delete_bindings ) )
			.result();
		co_return http::Response::error( 400, std::format( "Unable to schedule URL: {}", scheduled.error() ) );
	}

	downloader::DownloadSessionEventHub::instance().notify( session_id );

	const UrlRow row {
		job_id, parent_id, inserted_url, state, url_created_at, finished_at, error, note, record_id, std::nullopt
	};

	simdjson::builder::string_builder builder;
	builder.start_object();
	builder.escape_and_append_with_quotes( "session" );
	builder.append_colon();
	builder.start_object();
	appendSessionFields( builder, session_id, created_name, created_at, last_used_at, error_count );
	builder.end_object();
	builder.append_comma();
	builder.escape_and_append_with_quotes( "url" );
	builder.append_colon();
	builder.start_object();
	appendUrlFields( builder, row );
	builder.append_comma();
	builder.escape_and_append_with_quotes( "children" );
	builder.append_colon();
	builder.start_array();
	builder.end_array();
	builder.end_object();
	builder.end_object();

	co_return jsonResponse( builder, 201 );
}

static http::HTTPTask< http::Response > listUrls( db::DBContext& db, http::RequestContext context )
{
	const auto session_id_e { context.parameters().scalar< DownloadSessionID >( "session_id" ) };
	if ( !session_id_e ) co_return http::Response::error( 400, session_id_e.error().message );
	const auto session_id { *session_id_e };

	const auto flatten_e { context.parameters().optional< std::string >( "flatten" ) };
	if ( !flatten_e ) co_return http::Response::error( 400, flatten_e.error().message );
	const auto flatten_text { flatten_e->value_or( "" ) };
	const bool flatten { flatten_text == "1" || flatten_text == "true" || flatten_text == "yes" };

	if ( !( co_await touchSession( db, session_id ) ) )
		co_return http::Response::error( 404, std::format( "No download session with id {}", session_id ) );

	db::DBBindings bindings {};
	bindings.bind( session_id );
	const auto rows {
		co_await db
			.query(
				"SELECT download_session_url_id, parent_url_id, url, state, error, note, "
				"download_session_urls.record_id, "
				"(extract(epoch FROM download_session_records.imported_at) * 1000000)::bigint AS imported_at, "
				"extract(epoch FROM created_at)::bigint AS created_at, "
				"extract(epoch FROM finished_at)::bigint AS finished_at "
				"FROM download_session_urls "
				"LEFT JOIN download_session_records ON download_session_records.download_session_id = $1 "
				"AND download_session_records.record_id = download_session_urls.record_id "
				"WHERE download_session_urls.download_session_id = $1 "
				"ORDER BY download_session_urls.download_session_url_id",
				std::move( bindings ) )
			.rows<
				DownloadSessionUrlID,
				std::optional< DownloadSessionUrlID >,
				std::string,
				std::string,
				std::optional< std::string >,
				std::optional< std::string >,
				std::optional< RecordID >,
				std::optional< std::int64_t >,
				std::int64_t,
				std::optional< std::int64_t > >()
	};

	UrlNodes nodes {};
	UrlChildren children {};
	std::vector< DownloadSessionUrlID > roots {};

	for ( const auto& [ id, parent_id, url, state, error, note, record_id, imported_at, created_at, finished_at ] :
	      rows )
	{
		nodes.emplace(
			id, UrlRow { id, parent_id, url, state, created_at, finished_at, error, note, record_id, imported_at } );

		if ( parent_id )
			children[ *parent_id ].emplace_back( id );
		else
			roots.emplace_back( id );
	}

	std::ranges::reverse( roots );

	simdjson::builder::string_builder builder;
	builder.start_array();

	bool first_root { true };
	for ( const auto root : roots )
	{
		if ( !first_root ) builder.append_comma();
		first_root = false;

		if ( !flatten )
		{
			appendUrlNode( builder, root, nodes, children );
			continue;
		}

		std::vector< DownloadSessionUrlID > order {};
		std::unordered_map< RecordID, ImportedRecord > records {};
		collectDescendants( root, nodes, children, order, records );

		builder.start_object();
		appendUrlFields( builder, nodes.at( root ) );
		builder.append_comma();
		builder.escape_and_append_with_quotes( "urls" );
		builder.append_colon();
		builder.start_array();
		bool first_url { true };
		for ( const auto descendant : order )
		{
			if ( !first_url ) builder.append_comma();
			first_url = false;
			builder.start_object();
			appendUrlFields( builder, nodes.at( descendant ) );
			builder.end_object();
		}
		builder.end_array();
		builder.append_comma();

		std::vector< ImportedRecord > ordered_records {};
		ordered_records.reserve( records.size() );
		for ( const auto& [ _, imported ] : records ) ordered_records.emplace_back( imported );

		std::ranges::sort(
			ordered_records,
			[]( const ImportedRecord& left, const ImportedRecord& right )
			{
				if ( left.imported_at != right.imported_at ) return left.imported_at < right.imported_at;
				if ( left.url_id != right.url_id ) return left.url_id < right.url_id;
				return left.record_id < right.record_id;
			} );

		builder.escape_and_append_with_quotes( "record_ids" );
		builder.append_colon();
		builder.start_array();
		bool first_id { true };
		for ( const auto& imported : ordered_records )
		{
			if ( !first_id ) builder.append_comma();
			first_id = false;
			builder.append( imported.record_id );
		}
		builder.end_array();
		builder.end_object();
	}

	builder.end_array();

	co_return jsonResponse( builder );
}

static http::HTTPTask< http::Response > retryUrl( db::DBContext& db, http::RequestContext context )
{
	const auto session_id_e { context.parameters().scalar< DownloadSessionID >( "session_id" ) };
	if ( !session_id_e ) co_return http::Response::error( 400, session_id_e.error().message );
	const auto session_id { *session_id_e };

	const auto url_id_e { context.parameters().scalar< DownloadSessionUrlID >( "url_id" ) };
	if ( !url_id_e ) co_return http::Response::error( 400, url_id_e.error().message );
	const auto job_id { *url_id_e };

	if ( !( co_await touchSession( db, session_id ) ) )
		co_return http::Response::error( 404, std::format( "No download session with id {}", session_id ) );

	db::DBBindings existing_bindings {};
	existing_bindings.bind( job_id );
	existing_bindings.bind( session_id );
	const auto existing {
		co_await db
			.query(
				"SELECT url, state FROM download_session_urls "
				"WHERE download_session_url_id = $1 AND download_session_id = $2",
				std::move( existing_bindings ) )
			.rows< std::string, std::string >()
	};

	if ( existing.empty() )
		co_return http::Response::error(
			404, std::format( "No URL with id {} in download session {}", job_id, session_id ) );

	const auto& [ existing_url, existing_state ] { existing.row( 0 ) };

	if ( existing_state != "completed" && existing_state != "failed" )
		co_return http::Response::error(
			409, std::format( "URL {} is {} and cannot be retried yet", job_id, existing_state ) );

	db::DBBindings clear_children_bindings {};
	clear_children_bindings.bind( job_id );
	co_await db
		.query( "DELETE FROM download_session_urls WHERE parent_url_id = $1", std::move( clear_children_bindings ) )
		.result();

	db::DBBindings reset_bindings {};
	reset_bindings.bind( job_id );
	const auto reset {
		co_await db
			.query(
				"UPDATE download_session_urls SET state = 'pending', finished_at = NULL, error = NULL "
				"WHERE download_session_url_id = $1 AND state IN ('completed', 'failed') "
				"RETURNING download_session_url_id, parent_url_id, url, state, error, note, record_id, "
				"extract(epoch FROM created_at)::bigint AS created_at, "
				"extract(epoch FROM finished_at)::bigint AS finished_at",
				std::move( reset_bindings ) )
			.rows<
				DownloadSessionUrlID,
				std::optional< DownloadSessionUrlID >,
				std::string,
				std::string,
				std::optional< std::string >,
				std::optional< std::string >,
				std::optional< RecordID >,
				std::int64_t,
				std::optional< std::int64_t > >()
	};

	if ( reset.empty() )
		co_return http::Response::error( 409, std::format( "URL {} was restarted by another request", job_id ) );

	const auto& [ reset_id, parent_id, reset_url, reset_state, error, note, record_id, created_at, finished_at ] {
		reset.row( 0 )
	};

	const auto scheduled { downloader::downloadSessionManager().submit( job_id, session_id, existing_url ) };

	if ( !scheduled )
	{
		db::DBBindings fail_bindings {};
		fail_bindings.bind( job_id );
		fail_bindings.bind( scheduled.error() );
		co_await db
			.query(
				"UPDATE download_session_urls SET state = 'failed', finished_at = now(), error = $2 "
				"WHERE download_session_url_id = $1",
				std::move( fail_bindings ) )
			.result();
		co_return http::Response::error( 400, std::format( "Unable to reschedule URL: {}", scheduled.error() ) );
	}

	downloader::DownloadSessionEventHub::instance().notify( session_id );

	const UrlRow row {
		reset_id, parent_id, reset_url, reset_state, created_at, finished_at, error, note, record_id, std::nullopt
	};

	simdjson::builder::string_builder builder;
	builder.start_object();
	appendUrlFields( builder, row );
	builder.append_comma();
	builder.escape_and_append_with_quotes( "children" );
	builder.append_colon();
	builder.start_array();
	builder.end_array();
	builder.end_object();

	co_return jsonResponse( builder );
}

static http::HTTPTask< http::Response > listRecords( db::DBContext& db, http::RequestContext context )
{
	const auto session_id_e { context.parameters().scalar< DownloadSessionID >( "session_id" ) };
	if ( !session_id_e ) co_return http::Response::error( 400, session_id_e.error().message );
	const auto session_id { *session_id_e };

	if ( !( co_await touchSession( db, session_id ) ) )
		co_return http::Response::error( 404, std::format( "No download session with id {}", session_id ) );

	db::DBBindings bindings {};
	bindings.bind( session_id );
	const auto rows {
		co_await db
			.query(
				"SELECT record_id FROM download_session_records "
				"WHERE download_session_id = $1 ORDER BY imported_at DESC, record_id DESC",
				std::move( bindings ) )
			.rows< RecordID >()
	};

	simdjson::builder::string_builder builder;
	builder.start_object();
	builder.escape_and_append_with_quotes( "record_ids" );
	builder.append_colon();
	builder.start_array();
	bool first { true };
	for ( const auto& [ record_id ] : rows )
	{
		if ( !first ) builder.append_comma();
		first = false;
		builder.append( record_id );
	}
	builder.end_array();
	builder.end_object();

	co_return jsonResponse( builder );
}

static http::HTTPTask< http::Response > listErrors( db::DBContext& db, http::RequestContext context )
{
	const auto session_id_e { context.parameters().scalar< DownloadSessionID >( "session_id" ) };
	if ( !session_id_e ) co_return http::Response::error( 400, session_id_e.error().message );
	const auto session_id { *session_id_e };

	const auto status_e { context.parameters().optional< std::string >( "status" ) };
	if ( !status_e ) co_return http::Response::error( 400, status_e.error().message );

	std::vector< Int > statuses {};
	bool without_status { false };
	if ( !parseStatusFilter( status_e->value_or( "" ), statuses, without_status ) )
		co_return http::Response::error(
			400, "The status filter must be a comma separated list of status codes or 'none'" );

	const bool unfiltered { statuses.empty() && !without_status };

	if ( !( co_await touchSession( db, session_id ) ) )
		co_return http::Response::error( 404, std::format( "No download session with id {}", session_id ) );

	db::DBBindings count_bindings {};
	count_bindings.bind( session_id );
	const auto counts {
		co_await db
			.query(
				"SELECT status, count(*)::bigint AS count FROM download_session_errors "
				"WHERE download_session_id = $1 GROUP BY status ORDER BY status",
				std::move( count_bindings ) )
			.rows< std::optional< Int >, std::int64_t >()
	};

	db::DBBindings row_bindings {};
	row_bindings.bind( session_id );
	row_bindings.bind( unfiltered );
	row_bindings.bind( statuses );
	row_bindings.bind( without_status );
	const auto rows {
		co_await db
			.query(
				"SELECT download_session_error_id, download_session_url_id, url, status, lane, message, "
				"extract(epoch FROM occurred_at)::bigint AS occurred_at "
				"FROM download_session_errors "
				"WHERE download_session_id = $1 "
				"AND ($2 OR status = ANY($3::integer[]) OR ($4 AND status IS NULL)) "
				"ORDER BY download_session_error_id DESC LIMIT 1000",
				std::move( row_bindings ) )
			.rows<
				std::int64_t,
				std::optional< DownloadSessionUrlID >,
				std::string,
				std::optional< Int >,
				std::string,
				std::optional< std::string >,
				std::int64_t >()
	};

	simdjson::builder::string_builder builder;
	builder.start_object();
	builder.escape_and_append_with_quotes( "statuses" );
	builder.append_colon();
	builder.start_array();
	bool first_status { true };
	for ( const auto& [ status, count ] : counts )
	{
		if ( !first_status ) builder.append_comma();
		first_status = false;
		builder.start_object();
		builder.escape_and_append_with_quotes( "status" );
		builder.append_colon();
		if ( status )
			builder.append( *status );
		else
			builder.append_raw( "null" );
		builder.append_comma();
		builder.append_key_value( "count", count );
		builder.end_object();
	}
	builder.end_array();
	builder.append_comma();

	builder.escape_and_append_with_quotes( "errors" );
	builder.append_colon();
	builder.start_array();
	bool first_error { true };
	for ( const auto& [ id, url_id, url, status, lane, message, occurred_at ] : rows )
	{
		if ( !first_error ) builder.append_comma();
		first_error = false;
		builder.start_object();
		builder.append_key_value( "id", id );
		builder.append_comma();
		builder.escape_and_append_with_quotes( "url_id" );
		builder.append_colon();
		if ( url_id )
			builder.append( *url_id );
		else
			builder.append_raw( "null" );
		builder.append_comma();
		builder.append_key_value( "url", url );
		builder.append_comma();
		builder.escape_and_append_with_quotes( "status" );
		builder.append_colon();
		if ( status )
			builder.append( *status );
		else
			builder.append_raw( "null" );
		builder.append_comma();
		builder.append_key_value( "lane", lane );
		builder.append_comma();
		builder.escape_and_append_with_quotes( "message" );
		builder.append_colon();
		if ( message )
			builder.append( *message );
		else
			builder.append_raw( "null" );
		builder.append_comma();
		builder.append_key_value( "occurred_at", occurred_at );
		builder.end_object();
	}
	builder.end_array();
	builder.end_object();

	co_return jsonResponse( builder );
}

std::vector< http::RouteDefinition > downloadSessionRoutes( db::DBContext& db )
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

	add( proxygen::HTTPMethod::GET, "/download_sessions", {}, listSessions );
	add( proxygen::HTTPMethod::POST, "/download_sessions", {}, createSession );
	add( proxygen::HTTPMethod::GET,
	     "/download_sessions/{session_id}",
	     { downloadSessionIdPathParameter() },
	     getSession );
	add( proxygen::HTTPMethod::DELETE,
	     "/download_sessions/{session_id}",
	     { downloadSessionIdPathParameter() },
	     destroySession );
	add( proxygen::HTTPMethod::POST, "/download_sessions/urls", {}, submitUrlSession );
	add( proxygen::HTTPMethod::POST,
	     "/download_sessions/{session_id}/urls",
	     { downloadSessionIdPathParameter() },
	     submitUrl );
	add( proxygen::HTTPMethod::GET,
	     "/download_sessions/{session_id}/urls",
	     { downloadSessionIdPathParameter() },
	     listUrls );
	add( proxygen::HTTPMethod::POST,
	     "/download_sessions/{session_id}/urls/{url_id}/retry",
	     { downloadSessionIdPathParameter(), downloadSessionUrlIdPathParameter() },
	     retryUrl );
	add( proxygen::HTTPMethod::GET,
	     "/download_sessions/{session_id}/records",
	     { downloadSessionIdPathParameter() },
	     listRecords );
	add( proxygen::HTTPMethod::GET,
	     "/download_sessions/{session_id}/errors",
	     { downloadSessionIdPathParameter() },
	     listErrors );

	return routes;
}

} // namespace idhan::httpapi
