#include "downloadSessions.hpp"

#include <folly/io/IOBuf.h>
#include <json/writer.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
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
#include "downloader/DownloadSessionEventHub.hpp"
#include "downloader/DownloadSessionManager.hpp"
#include "IDHANDownloader/urls/UrlNormalization.hpp"
#include "downloader/SessionRowObserver.hpp"
#include "downloader/SessionUrlIds.hpp"
#include "downloader/downloaderEvents.hpp"
#include "downloader/sessionRows.hpp"
#include "downloaderJson.hpp"
#include "idhan/db/DBContext.hpp"
#include "idhan/http/routing/ReflectedRoutes.hpp"
#include "jsonBody.hpp"

namespace idhan::httpapi
{

constexpr std::size_t max_name_length { 200 };
constexpr std::size_t max_url_length { 8192 };

//! Returns the new last_used_at, or nothing when the session does not exist.
static http::HTTPTask< std::optional< std::int64_t > > touchSession(
	db::DBContext& db,
	const DownloadSessionID session_id )
{
	db::DBBindings bindings {};
	bindings.bind( session_id );
	const auto rows {
		co_await db
			.query(
				//language=postgresql
				R"SQL(
					UPDATE
					    download_sessions
					SET last_used_at = now()
					WHERE download_session_id = $1
					RETURNING extract(epoch FROM last_used_at)::bigint
				)SQL",
				std::move( bindings ) )
			.rows< std::int64_t >()
	};
	if ( rows.empty() ) co_return std::nullopt;

	co_return std::get< 0 >( rows.row( 0 ) );
}

[[nodiscard]] static DownloadSessionResponse sessionResponse(
	const DownloadSessionID id,
	const std::string& name,
	const std::int64_t created_at,
	const std::int64_t last_used_at,
	const std::int64_t error_count,
	const bool is_subscription = false )
{
	const auto network { downloader::downloadSessionManager().networkSnapshot( id ) };
	return {
		id,
		name,
		created_at,
		last_used_at,
		error_count,
		static_cast< std::uint64_t >( network.downloading ),
		static_cast< std::uint64_t >( network.waiting ),
		static_cast< std::uint64_t >( network.waiting_parsers ),
		static_cast< std::uint64_t >( network.waiting_requests ),
		static_cast< std::uint64_t >( network.waiting_http_rate ),
		static_cast< std::uint64_t >( network.waiting_http_shard ),
		static_cast< std::uint64_t >( network.waiting_storage ),
		network.download_bytes_per_second,
		network.downloaded_bytes,
		network.average_import_bytes,
		is_subscription
	};
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
	std::optional< std::string > stage {};
	std::optional< RecordID > record_id {};
	std::optional< std::int64_t > imported_at {};
	std::optional< std::int64_t > size {};
};

template < typename T >
[[nodiscard]] static T urlResponse( const UrlRow& row )
{
	T result {};
	result.id = row.id;
	result.parent_id = row.parent_id;
	result.url = row.url;
	result.state = row.state;
	result.created_at = row.created_at;
	result.finished_at = row.finished_at;
	result.error = row.error;
	result.stage = row.stage;
	result.record_id = row.record_id;
	result.size = row.size;
	return result;
}

struct ImportedRecord
{
	RecordID record_id {};
	std::int64_t imported_at {};
	DownloadSessionUrlID url_id {};
};

[[nodiscard]] static bool importedRecordBefore( const ImportedRecord& left, const ImportedRecord& right )
{
	if ( left.imported_at != right.imported_at ) return left.imported_at < right.imported_at;

	if ( left.url_id != right.url_id ) return left.url_id < right.url_id;

	return left.record_id < right.record_id;
}

using UrlNodes = std::unordered_map< DownloadSessionUrlID, UrlRow >;
using UrlChildren = std::unordered_map< DownloadSessionUrlID, std::vector< DownloadSessionUrlID > >;

[[nodiscard]] static DownloadUrlNodeResponse urlNodeResponse(
	const DownloadSessionUrlID id,
	const UrlNodes& nodes,
	const UrlChildren& children )
{
	auto result { urlResponse< DownloadUrlNodeResponse >( nodes.at( id ) ) };
	const auto found { children.find( id ) };
	if ( found != children.end() )
	{
		result.children.reserve( found->second.size() );
		for ( const auto child : found->second ) result.children.push_back( urlNodeResponse( child, nodes, children ) );
	}

	return result;
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

static http::HTTPTask< std::string > freeSessionName( db::DBContext& db, const std::string& url )
{
	const std::string base { url.size() > 180 ? url.substr( 0, 180 ) : url };

	for ( int suffix { 1 }; suffix < 1000; ++suffix )
	{
		const std::string candidate { suffix == 1 ? base : std::format( "{} ({})", base, suffix ) };
		db::DBBindings bindings {};
		bindings.bind( candidate );
		const auto taken {
			co_await db
					 .query(
						 //language=postgresql
						 R"SQL(
						SELECT
						    1
						FROM download_sessions
						WHERE lower(name) = lower($1)
					)SQL",
						 std::move( bindings ) )
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

http::HTTPTask< http::JsonResult< std::vector< DownloadSessionResponse > > > listSessions(
	db::DBContext& db,
	[[maybe_unused]] http::RequestContext context )
{
	const auto rows {
		co_await db
			.query(
				//language=postgresql
				R"SQL(
					SELECT
					    download_session_id,
					    name,
					    extract(epoch FROM created_at)::bigint AS created_at,
					    extract(epoch FROM last_used_at)::bigint AS last_used_at,
					    (
					        SELECT
					            count(*)
					        FROM download_session_errors
					        WHERE download_session_errors.download_session_id = download_sessions.download_session_id
					    ) AS error_count
					FROM download_sessions
					WHERE NOT is_subscription
					ORDER BY
					    last_used_at DESC,
					    download_session_id DESC
				)SQL" )
			.rows< DownloadSessionID, std::string, std::int64_t, std::int64_t, std::int64_t >()
	};

	std::vector< DownloadSessionResponse > response;
	response.reserve( rows.size() );
	for ( const auto& [ id, name, created_at, last_used_at, error_count ] : rows )
		response.push_back( sessionResponse( id, name, created_at, last_used_at, error_count ) );

	co_return http::JsonResponse< std::vector< DownloadSessionResponse > > { std::move( response ) };
}

http::HTTPTask< http::JsonResult< DownloadSessionResponse, 201 > > createSession(
	db::DBContext& db,
	http::JsonBody< CreateSessionBody > body )
{
	const auto& name { body.value.name };
	if ( name.empty() || name.size() > max_name_length )
		co_return http::Response::error(
			400, "name must be 1 to " + std::to_string( max_name_length ) + " characters" );

	db::DBBindings existing_bindings {};
	existing_bindings.bind( name );
	const auto existing {
		co_await db
				 .query(
					 //language=postgresql
					 R"SQL(
					SELECT
					    1
					FROM download_sessions
					WHERE lower(name) = lower($1)
				)SQL",
					 std::move( existing_bindings ) )
				 .rows< std::int32_t >()
	};

	if ( !existing.empty() )
		co_return http::Response::error( 409, std::format( "A download session named '{}' already exists", name ) );

	db::DBBindings insert_bindings {};
	insert_bindings.bind( name );
	const auto created {
		co_await db
			.query(
				//language=postgresql
				R"SQL(
					INSERT INTO download_sessions (name)
					    VALUES ($1)
					RETURNING download_session_id, name, extract(epoch FROM created_at)::bigint AS created_at, extract(epoch FROM last_used_at)::bigint AS last_used_at, 0::bigint AS error_count
				)SQL",
				std::move( insert_bindings ) )
			.rows< DownloadSessionID, std::string, std::int64_t, std::int64_t, std::int64_t >()
	};

	const auto& [ id, created_name, created_at, last_used_at, error_count ] { created.row( 0 ) };
	downloader::SessionUrlIds::instance().create( id );
	downloader::events::sessionCreated(
		downloader::events::SessionInfo {
			.id = id, .name = created_name, .created_at = created_at, .last_used_at = last_used_at } );

	co_return http::JsonResponse< DownloadSessionResponse, 201 > {
		sessionResponse( id, created_name, created_at, last_used_at, error_count )
	};
}

http::HTTPTask< http::JsonResult< DownloadSessionResponse > > getSession(
	db::DBContext& db,
	http::RequestContext context )
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
				//language=postgresql
				R"SQL(
					SELECT
					    download_session_id,
					    name,
					    extract(epoch FROM created_at)::bigint AS created_at,
					    extract(epoch FROM last_used_at)::bigint AS last_used_at,
					    (
					        SELECT
					            count(*)
					        FROM download_session_errors
					        WHERE download_session_errors.download_session_id = download_sessions.download_session_id
					    ) AS error_count,
					    is_subscription
					FROM download_sessions
					WHERE download_session_id = $1
				)SQL",
				std::move( bindings ) )
			.rows< DownloadSessionID, std::string, std::int64_t, std::int64_t, std::int64_t, bool >()
	};

	const auto& [ id, name, created_at, last_used_at, error_count, is_subscription ] { rows.row( 0 ) };

	co_return http::JsonResponse< DownloadSessionResponse > {
		sessionResponse( id, name, created_at, last_used_at, error_count, is_subscription )
	};
}

http::HTTPTask< http::JsonResult< DownloaderDeletedResponse > > destroySession(
	db::DBContext& db,
	http::RequestContext context )
{
	const auto session_id_e { context.parameters().scalar< DownloadSessionID >( "session_id" ) };
	if ( !session_id_e ) co_return http::Response::error( 400, session_id_e.error().message );
	const auto session_id { *session_id_e };

	if ( !( co_await touchSession( db, session_id ) ) )
		co_return http::Response::error( 404, std::format( "No download session with id {}", session_id ) );

	co_await downloader::downloadSessionManager().destroy( session_id );

	db::DBBindings bindings {};
	bindings.bind( session_id );
	const auto deleted {
		co_await db
				 .query(
					 //language=postgresql
					 R"SQL(
					DELETE FROM download_sessions
					WHERE download_session_id = $1
				)SQL",
					 std::move( bindings ) )
				 .result()
	};

	if ( std::atoll( PQcmdTuples( deleted.get() ) ) == 0 )
		co_return http::Response::error( 404, std::format( "No download session with id {}", session_id ) );

	downloader::DownloadSessionEventHub::instance().removed( session_id );
	downloader::SessionUrlIds::instance().remove( session_id );

	co_return http::JsonResponse< DownloaderDeletedResponse > {};
}

http::HTTPTask< http::JsonResult< DownloadOptionsResponse > > downloadOptions( db::DBContext&, http::RequestContext )
{
	const auto options_e { co_await downloader::downloadSessionManager().downloadOptions() };
	if ( !options_e.has_value() ) co_return http::Response::error( 503, options_e.error() );

	auto response_e { decodeDownloaderJson< DownloadOptionsResponse >( options_e.value() ) };
	if ( !response_e.has_value() ) co_return http::Response::error( 500, response_e.error() );

	co_return http::JsonResponse< DownloadOptionsResponse > { std::move( response_e.value() ) };
}

http::HTTPTask< http::JsonResult< ParserInstalledResponse > > uploadParser(
	db::DBContext&,
	http::JsonBody< UploadParserBody > body )
{
	const auto installed_e { co_await downloader::downloadSessionManager().installParser(
		std::move( body.value.filename ), std::move( body.value.source ), body.value.replace.value_or( false ) ) };
	if ( !installed_e.has_value() )
		co_return http::Response::error(
			installed_e.error().starts_with( "Parser already exists" ) ? 409 : 400, installed_e.error() );

	co_return http::JsonResponse< ParserInstalledResponse > {};
}

http::HTTPTask< http::EncodedResult< std::string, "text/javascript" > > downloadParser(
	db::DBContext&,
	http::RequestContext context )
{
	const auto filename_e { context.parameters().scalar< std::string >( "filename" ) };
	if ( !filename_e.has_value() ) co_return http::Response::error( 400, filename_e.error().message );

	const auto source_e { co_await downloader::downloadSessionManager().parserSource( filename_e.value() ) };
	if ( !source_e.has_value() ) co_return http::Response::error( 404, source_e.error() );

	auto response { http::Response::bytes( 200, folly::IOBuf::copyBuffer( source_e.value() ), "text/javascript" ) };
	response.message.getHeaders().add( "Content-Disposition", "attachment; filename=\"" + filename_e.value() + "\"" );
	co_return http::encodedResponse< std::string, "text/javascript" >( std::move( response ) );
}

http::HTTPTask< http::JsonResult< ParserListResponse > > listParsers( db::DBContext&, http::RequestContext )
{
	const auto parsers_e { co_await downloader::downloadSessionManager().parsers() };
	if ( !parsers_e.has_value() ) co_return http::Response::error( 503, parsers_e.error() );

	auto response_e { decodeDownloaderJson< std::vector< ParserMetadataResponse > >( parsers_e.value() ) };
	if ( !response_e.has_value() ) co_return http::Response::error( 500, response_e.error() );

	co_return http::JsonResponse< ParserListResponse > { { std::move( response_e.value() ) } };
}

[[nodiscard]] static http::HTTPTask< std::expected< std::string, http::Response > > parseDownloadOptions(
	http::possiblyMissing< SubmitUrlSessionBodyOptions > options )
{
	if ( !options ) co_return "{}";

	auto text_e { http::serializeJson( *options ) };
	if ( !text_e.has_value() ) co_return std::unexpected( http::Response::error( 400, text_e.error().message ) );

	simdjson::dom::parser parser;
	const simdjson::dom::element settings { parser.parse( text_e.value() ) };

	const auto known_e { co_await downloader::downloadSessionManager().downloadOptions() };
	if ( !known_e.has_value() ) co_return std::unexpected( http::Response::error( 503, known_e.error() ) );

	for ( const auto field : settings.get_object().value() )
	{
		if ( field.key == "force_import" )
		{
			if ( !field.value.is_bool() )
				co_return std::unexpected( http::Response::error( 400, "force_import must be a boolean" ) );

			continue;
		}

		if ( field.key != "parsers" || !field.value.is_object() )
			co_return std::unexpected(
				http::Response::error( 400, "Unknown download option or invalid parsers object" ) );

		for ( const auto parser_field : field.value.get_object().value() )
		{
			const Json::Value* metadata {};
			for ( const auto& candidate : known_e.value()[ "parsers" ] )
				if ( candidate[ "id" ].asString() == parser_field.key ) metadata = &candidate;

			if ( metadata == nullptr || !parser_field.value.is_object() )
				co_return std::unexpected( http::Response::error( 400, "Unknown parser or invalid parser options" ) );

			for ( const auto option : parser_field.value.get_object().value() )
			{
				const Json::Value* definition {};
				for ( const auto& candidate : ( *metadata )[ "options" ] )
					if ( candidate[ "name" ].asString() == option.key ) definition = &candidate;

				if ( definition == nullptr )
					co_return std::unexpected( http::Response::error( 400, "Unknown parser option" ) );

				const auto type { ( *definition )[ "type" ].asString() };
				bool valid { false };
				if ( type == "boolean" ) valid = option.value.is_bool();

				if ( type == "string" ) valid = option.value.is_string();

				if ( type == "number" )
				{
					double value {};
					valid = option.value.get_double().get( value ) == simdjson::SUCCESS;
					if ( valid && ( *definition )[ "minimum" ].isNumeric() )
						valid = value >= ( *definition )[ "minimum" ].asDouble();

					if ( valid && ( *definition )[ "integer" ].asBool() ) valid = std::trunc( value ) == value;
				}

				if ( type == "string[]" && option.value.is_array() )
				{
					valid = true;
					for ( const auto entry : option.value.get_array().value() ) valid = valid && entry.is_string();
				}

				if ( !valid )
					co_return std::unexpected(
						http::Response::error(
							400, std::format( "Invalid value for {}.{}", parser_field.key, option.key ) ) );
			}
		}
	}
	co_return simdjson::to_string( settings );
}

static http::HTTPTask< std::expected< downloader::rows::InsertedUrl, std::string > > createRootUrl(
	db::DBContext& db,
	const DownloadSessionID session_id,
	const std::string& url )
{
	auto inserted_e { co_await downloader::rows::insert( session_id, 0, url, "pending", db ) };
	if ( !inserted_e.has_value() ) co_return inserted_e;

	const auto& inserted { inserted_e.value() };
	downloader::events::urlCreated(
		session_id,
		downloader::events::UrlRow {
			.url_id = inserted.id,
			.url = url,
			.created_at = inserted.created_at,
			.state = downloader::events::UrlState { .state = "pending" } } );
	co_return inserted_e;
}

static http::HTTPTask< void > deleteRootUrl(
	db::DBContext& db,
	const DownloadSessionID session_id,
	const DownloadSessionUrlID url_id )
{
	db::DBBindings bindings {};
	bindings.bind( session_id );
	bindings.bind( url_id );
	co_await db
		.query(
			//language=postgresql
			R"SQL(
				DELETE FROM download_session_urls
				WHERE download_session_id = $1
				    AND download_session_url_id = $2
			)SQL",
			std::move( bindings ) )
		.result();
	downloader::events::urlRemoved( session_id, url_id );
}

http::HTTPTask< http::JsonResult< SubmittedDownloadUrlResponse, 201 > > submitUrl(
	db::DBContext& db,
	http::RequestContext context,
	http::JsonBody< SubmitUrlBody > body )
{
	const auto session_id_e { context.parameters().scalar< DownloadSessionID >( "session_id" ) };
	if ( !session_id_e ) co_return http::Response::error( 400, session_id_e.error().message );
	const auto session_id { *session_id_e };

	if ( body.value.url.empty() ) co_return http::Response::error( 400, "url must not be empty" );

	const auto url { downloader::withDefaultScheme( body.value.url ) };
	if ( url.size() > max_url_length )
		co_return http::Response::error( 400, "url must be 1 to " + std::to_string( max_url_length ) + " characters" );

	const auto last_used_at { co_await touchSession( db, session_id ) };
	if ( !last_used_at.has_value() )
		co_return http::Response::error( 404, std::format( "No download session with id {}", session_id ) );

	const auto inserted_e { co_await createRootUrl( db, session_id, url ) };
	if ( !inserted_e.has_value() ) co_return http::Response::error( 500, inserted_e.error() );

	const auto job_id { inserted_e.value().id };

	const auto saved_settings { co_await downloader::DownloadSessionManager::sessionSettings( db, session_id ) };
	const auto scheduled {
		co_await downloader::downloadSessionManager().submit( job_id, session_id, url, saved_settings, last_used_at )
	};

	if ( !scheduled )
	{
		co_await deleteRootUrl( db, session_id, job_id );
		co_return http::Response::error( 400, std::format( "Unable to schedule URL: {}", scheduled.error() ) );
	}

	co_return http::JsonResponse< SubmittedDownloadUrlResponse, 201 > { { job_id, url, "pending" } };
}

http::HTTPTask< http::JsonResult< SubmittedDownloadSessionResponse, 201 > > submitUrlSession(
	db::DBContext& db,
	http::JsonBody< SubmitUrlSessionBody > body )
{
	if ( body.value.url.empty() ) co_return http::Response::error( 400, "url must not be empty" );

	const auto url { downloader::withDefaultScheme( body.value.url ) };
	if ( url.size() > max_url_length )
		co_return http::Response::error( 400, "url must be 1 to " + std::to_string( max_url_length ) + " characters" );

	auto settings_e { co_await parseDownloadOptions( body.value.options ) };
	if ( !settings_e.has_value() ) co_return std::move( settings_e.error() );

	const auto& settings { *settings_e };

	const auto name { body.value.name ? *body.value.name : co_await freeSessionName( db, url ) };

	if ( name.empty() || name.size() > max_name_length )
		co_return http::Response::error( 400, "Session name must be 1 to 200 characters" );

	db::DBBindings existing_bindings {};
	existing_bindings.bind( name );
	const auto existing {
		co_await db
				 .query(
					 //language=postgresql
					 R"SQL(
					SELECT
					    1
					FROM download_sessions
					WHERE lower(name) = lower($1)
				)SQL",
					 std::move( existing_bindings ) )
				 .rows< std::int32_t >()
	};

	if ( !existing.empty() )
		co_return http::Response::error( 409, std::format( "A download session named '{}' already exists", name ) );

	db::DBBindings insert_bindings {};
	insert_bindings.bind( name );
	insert_bindings.bind( settings );
	const auto created {
		co_await db
			.query(
				//language=postgresql
				R"SQL(
					INSERT INTO download_sessions (name, options)
					    VALUES ($1, $2::jsonb)
					RETURNING download_session_id, name, extract(epoch FROM created_at)::bigint AS created_at, extract(epoch FROM last_used_at)::bigint AS last_used_at, 0::bigint AS error_count
				)SQL",
				std::move( insert_bindings ) )
			.rows< DownloadSessionID, std::string, std::int64_t, std::int64_t, std::int64_t >()
	};

	const auto& [ session_id, created_name, created_at, last_used_at, error_count ] { created.row( 0 ) };
	downloader::SessionUrlIds::instance().create( session_id );
	downloader::events::sessionCreated(
		downloader::events::SessionInfo {
			.id = session_id, .name = created_name, .created_at = created_at, .last_used_at = last_used_at } );

	const auto inserted_e { co_await createRootUrl( db, session_id, url ) };
	if ( !inserted_e.has_value() ) co_return http::Response::error( 500, inserted_e.error() );

	const auto& inserted { inserted_e.value() };
	const auto job_id { inserted.id };

	const auto saved_settings { co_await downloader::DownloadSessionManager::sessionSettings( db, session_id ) };
	const auto scheduled {
		co_await downloader::downloadSessionManager().submit( job_id, session_id, url, saved_settings, last_used_at )
	};

	if ( !scheduled )
	{
		db::DBBindings delete_bindings {};
		delete_bindings.bind( session_id );
		co_await db
				 .query(
					 //language=postgresql
					 R"SQL(
					DELETE FROM download_sessions
					WHERE download_session_id = $1
				)SQL",
					 std::move( delete_bindings ) )
				 .result();
		downloader::DownloadSessionEventHub::instance().removed( session_id );
		downloader::SessionUrlIds::instance().remove( session_id );
		co_return http::Response::error( 400, std::format( "Unable to schedule URL: {}", scheduled.error() ) );
	}

	const UrlRow row { job_id,       std::nullopt, url,          "pending",    inserted.created_at,
	                   std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt };

	co_return http::JsonResponse< SubmittedDownloadSessionResponse, 201 > {
		{ sessionResponse( session_id, created_name, created_at, last_used_at, error_count ),
		  urlResponse< DownloadUrlNodeResponse >( row ) }
	};
}

http::HTTPTask< http::JsonResult< DownloadUrlListResponse > > listUrls(
	db::DBContext& db,
	http::RequestContext context,
	http::Query< ListUrlsQuery > query )
{
	const auto session_id_e { context.parameters().scalar< DownloadSessionID >( "session_id" ) };
	if ( !session_id_e ) co_return http::Response::error( 400, session_id_e.error().message );
	const auto session_id { *session_id_e };

	const auto flatten_text { query.value.flatten.value_or( "" ) };
	const bool flatten { flatten_text == "1" || flatten_text == "true" || flatten_text == "yes" };

	if ( !( co_await touchSession( db, session_id ) ) )
		co_return http::Response::error( 404, std::format( "No download session with id {}", session_id ) );

	db::DBBindings bindings {};
	bindings.bind( session_id );
	const auto rows {
		co_await db
			.query(
				//language=postgresql
				R"SQL(
					SELECT
					    download_session_urls.download_session_url_id,
					    parent_url_id,
					    url,
					    state,
					    failures.message,
					    failures.stage,
					    download_session_urls.record_id,
					    (extract(epoch FROM download_session_records.imported_at) * 1000000)::bigint AS imported_at,
					    extract(epoch FROM created_at)::bigint AS created_at,
					    extract(epoch FROM finished_at)::bigint AS finished_at,
					    file_info.size
					FROM download_session_urls
					    LEFT JOIN download_session_url_failures failures ON failures.download_session_id = download_session_urls.download_session_id
					        AND failures.download_session_url_id = download_session_urls.download_session_url_id
					    LEFT JOIN file_info ON file_info.record_id = download_session_urls.record_id
					    LEFT JOIN download_session_records ON download_session_records.download_session_id = $1
					        AND download_session_records.record_id = download_session_urls.record_id
					WHERE download_session_urls.download_session_id = $1
					ORDER BY
					    download_session_urls.download_session_url_id
				)SQL",
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
				std::optional< std::int64_t >,
				std::optional< std::int64_t > >()
	};

	UrlNodes nodes {};
	UrlChildren children {};
	std::vector< DownloadSessionUrlID > roots {};

	for (
		const auto& [ id, parent_id, url, state, error, stage, record_id, imported_at, created_at, finished_at, size ] :
		rows )
	{
		nodes.emplace(
			id,
			UrlRow { id, parent_id, url, state, created_at, finished_at, error, stage, record_id, imported_at, size } );

		if ( parent_id )
			children[ *parent_id ].emplace_back( id );
		else
			roots.emplace_back( id );
	}

	std::ranges::reverse( roots );

	if ( !flatten )
	{
		std::vector< DownloadUrlNodeResponse > response;
		response.reserve( roots.size() );
		for ( const auto root : roots ) response.push_back( urlNodeResponse( root, nodes, children ) );

		co_return http::JsonResponse< DownloadUrlListResponse > { std::move( response ) };
	}

	std::vector< DownloadUrlFlatResponse > response;
	response.reserve( roots.size() );
	for ( const auto root : roots )
	{
		auto item { urlResponse< DownloadUrlFlatResponse >( nodes.at( root ) ) };
		std::vector< DownloadSessionUrlID > order;
		std::unordered_map< RecordID, ImportedRecord > records;
		collectDescendants( root, nodes, children, order, records );
		item.urls.reserve( order.size() );
		for ( const auto descendant : order )
			item.urls.push_back( urlResponse< DownloadUrlResponse >( nodes.at( descendant ) ) );

		std::vector< ImportedRecord > ordered_records;
		ordered_records.reserve( records.size() );
		for ( const auto& [ _, imported ] : records ) ordered_records.push_back( imported );

		std::ranges::sort( ordered_records, importedRecordBefore );
		item.record_ids.reserve( ordered_records.size() );
		for ( const auto& imported : ordered_records ) item.record_ids.push_back( imported.record_id );

		response.push_back( std::move( item ) );
	}

	co_return http::JsonResponse< DownloadUrlListResponse > { std::move( response ) };
}

http::HTTPTask< http::JsonResult< DownloadUrlNodeResponse > > retryUrl(
	db::DBContext& db,
	http::RequestContext context )
{
	const auto session_id_e { context.parameters().scalar< DownloadSessionID >( "session_id" ) };
	if ( !session_id_e ) co_return http::Response::error( 400, session_id_e.error().message );
	const auto session_id { *session_id_e };

	const auto url_id_e { context.parameters().scalar< DownloadSessionUrlID >( "url_id" ) };
	if ( !url_id_e ) co_return http::Response::error( 400, url_id_e.error().message );
	const auto job_id { *url_id_e };

	const auto last_used_at { co_await touchSession( db, session_id ) };
	if ( !last_used_at.has_value() )
		co_return http::Response::error( 404, std::format( "No download session with id {}", session_id ) );

	db::DBBindings existing_bindings {};
	existing_bindings.bind( job_id );
	existing_bindings.bind( session_id );
	const auto existing {
		co_await db
			.query(
				//language=postgresql
				R"SQL(
					SELECT
					    url,
					    state
					FROM download_session_urls
					WHERE download_session_url_id = $1
					    AND download_session_id = $2
				)SQL",
				std::move( existing_bindings ) )
			.rows< std::string, std::string >()
	};

	if ( existing.empty() )
		co_return http::Response::error(
			404, std::format( "No URL with id {} in download session {}", job_id, session_id ) );

	const auto& [ existing_url, existing_state ] { existing.row( 0 ) };

	const bool retryable { existing_state == "completed" || existing_state == "failed" || existing_state == "imported"
		                   || existing_state == "imported_previously" };
	if ( !retryable )
		co_return http::Response::error(
			409, std::format( "URL {} is {} and cannot be retried", job_id, existing_state ) );

	const auto flagged_e { co_await downloader::rows::flagErrorsForRetry( session_id, job_id, db ) };
	if ( !flagged_e.has_value() ) co_return http::Response::error( 500, flagged_e.error() );

	const auto removed_e { co_await downloader::rows::removeChildren( session_id, job_id, db ) };
	if ( !removed_e.has_value() ) co_return http::Response::error( 500, removed_e.error() );

	for ( const auto child : removed_e.value() ) downloader::events::urlRemoved( session_id, child );

	db::DBBindings reset_bindings {};
	reset_bindings.bind( session_id );
	reset_bindings.bind( job_id );
	const auto reset {
		co_await db
			.query(
				//language=postgresql
				R"SQL(
					WITH reset AS (
					    UPDATE
					        download_session_urls
					    SET state = 'pending',
					        finished_at = NULL,
					        record_id = NULL
					    WHERE download_session_id = $1
					        AND download_session_url_id = $2
					        AND state IN ('completed', 'failed', 'imported', 'imported_previously')
					    RETURNING parent_url_id,
					        extract(epoch FROM created_at)::bigint AS created_at
					),
					cleared AS (
					    DELETE FROM download_session_url_failures
					    WHERE download_session_id = $1
					        AND download_session_url_id = $2
					        AND EXISTS (
					            SELECT
					                1
					            FROM reset
					        )
					)
					SELECT
					    parent_url_id,
					    created_at
					FROM reset
				)SQL",
				std::move( reset_bindings ) )
			.rows< std::optional< DownloadSessionUrlID >, std::int64_t >()
	};

	if ( reset.empty() )
		co_return http::Response::error( 409, std::format( "URL {} was restarted by another request", job_id ) );

	const auto& [ parent_id, created_at ] { reset.row( 0 ) };
	// Announced before submitting, so the work's own updates cannot arrive ahead of the reset.
	downloader::events::urlUpdated( session_id, job_id, downloader::events::UrlState { .state = "pending" } );

	const auto saved_settings { co_await downloader::DownloadSessionManager::sessionSettings( db, session_id ) };
	const auto scheduled { co_await downloader::downloadSessionManager().submit(
		job_id, session_id, existing_url, saved_settings, last_used_at ) };

	if ( !scheduled )
	{
		const auto stage { downloader::failureStage( downloader::FailureStage::CLOSED ) };
		const auto failed_e { co_await downloader::rows::fail( session_id, job_id, stage, scheduled.error(), db ) };
		if ( !failed_e.has_value() ) co_return http::Response::error( 500, failed_e.error() );

		downloader::events::urlUpdated(
			session_id,
			job_id,
			downloader::events::UrlState {
				.state = "failed", .stage = stage, .error = scheduled.error(), .finished_at = failed_e.value() } );
		co_return http::Response::error( 400, std::format( "Unable to reschedule URL: {}", scheduled.error() ) );
	}

	const UrlRow row { job_id,       parent_id,    existing_url, "pending",    created_at,
	                   std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt };

	co_return http::JsonResponse< DownloadUrlNodeResponse > { urlResponse< DownloadUrlNodeResponse >( row ) };
}

http::HTTPTask< http::JsonResult< DownloadSessionRecordsResponse > > listRecords(
	db::DBContext& db,
	http::RequestContext context )
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
				//language=postgresql
				R"SQL(
					SELECT
					    record_id
					FROM download_session_records
					WHERE download_session_id = $1
					ORDER BY
					    imported_at DESC,
					    record_id DESC
				)SQL",
				std::move( bindings ) )
			.rows< RecordID >()
	};

	DownloadSessionRecordsResponse response {};
	response.record_ids.reserve( rows.size() );
	for ( const auto& [ record_id ] : rows ) response.record_ids.push_back( record_id );

	co_return http::JsonResponse< DownloadSessionRecordsResponse > { std::move( response ) };
}

http::HTTPTask< http::JsonResult< DownloadSessionErrorsResponse > > listErrors(
	db::DBContext& db,
	http::RequestContext context,
	http::Query< ListErrorsQuery > query )
{
	const auto session_id_e { context.parameters().scalar< DownloadSessionID >( "session_id" ) };
	if ( !session_id_e ) co_return http::Response::error( 400, session_id_e.error().message );
	const auto session_id { *session_id_e };

	std::vector< Int > statuses {};
	bool without_status { false };
	if ( !parseStatusFilter( query.value.status.value_or( "" ), statuses, without_status ) )
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
				//language=postgresql
				R"SQL(
					SELECT
					    status,
					    count(*)::bigint AS count
					FROM download_session_errors
					WHERE download_session_id = $1
					GROUP BY
					    status
					ORDER BY
					    status
				)SQL",
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
				//language=postgresql
				R"SQL(
					SELECT
					    download_session_error_id,
					    download_session_url_id,
					    url,
					    status,
					    lane,
					    message,
					    extract(epoch FROM occurred_at)::bigint AS occurred_at
					FROM download_session_errors
					WHERE download_session_id = $1
					    AND ($2
					        OR status = ANY ($3::integer[])
					        OR ($4
					            AND status IS NULL))
					ORDER BY
					    download_session_error_id DESC
					LIMIT 1000
				)SQL",
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

	DownloadSessionErrorsResponse response {};
	response.statuses.reserve( counts.size() );
	for ( const auto& [ status, count ] : counts ) response.statuses.push_back( { status, count } );

	response.errors.reserve( rows.size() );
	for ( const auto& [ id, url_id, url, status, lane, message, occurred_at ] : rows )
		response.errors.push_back( { id, url_id, url, status, lane, message, occurred_at } );

	co_return http::JsonResponse< DownloadSessionErrorsResponse > { std::move( response ) };
}

} // namespace idhan::httpapi
