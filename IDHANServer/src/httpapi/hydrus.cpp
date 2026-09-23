#include "hydrus.hpp"

#include <folly/io/IOBuf.h>
#include <json/json.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <optional>
#include <random>
#include <simdjson.h>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "IDHANTypes.hpp"
#include "apiAuth.hpp"
#include "apiVersion.hpp"
#include "hyapi/constants/UrlTypes.hpp"
#include "hyapi/constants/header-names.hpp"
#include "hyapi/constants/hydrus_version.hpp"
#include "hyapi/services/Services.hpp"
#include "hydrus/ClientConstants_gen.hpp"
#include "hydrus/ClientGUIPagesCore_gen.hpp"
#include "idhan/SHA256.hpp"
#include "idhan/db/DBContext.hpp"
#include "jsonBody.hpp"
#include "recordFile.hpp"
#include "records.hpp"
#include "search/SearchQuery.hpp"
#include "tags.hpp"
#include "thumbnail.hpp"
#include "urls/UrlDomain.hpp"

namespace idhan::httpapi
{

[[nodiscard]] static http::Response hyAuthError(
	const std::uint16_t status,
	const std::string_view message,
	const std::string_view exception_type )
{
	simdjson::builder::string_builder builder;
	builder.start_object();
	builder.append_key_value( "error", message );
	builder.append_comma();
	builder.append_key_value( "exception_type", exception_type );
	builder.append_comma();
	builder.append_key_value( "status_code", status );
	builder.end_object();

	auto json { builder.view() };
	if ( json.error() ) throw std::runtime_error( simdjson::error_message( json.error() ) );

	return http::Response::json( status, folly::IOBuf::copyBuffer( *json ) );
}

[[nodiscard]] static http::RequestValidation hyAuthValidation( db::DBContext& db )
{
	return [ &db ]( const proxygen::HTTPMessage& message, const http::RequestParameters& parameters )
	           -> http::HTTPTask< std::optional< http::Response > >
	{
#ifdef IDHAN_DISABLE_API_AUTH
		co_return std::nullopt;
#else
		const auto param_e { parameters.optional< std::string >( HY_ACCESS_KEY_HEADER_NAME ) };

		std::string key {};
		if ( param_e && *param_e && !( *param_e )->empty() ) key = **param_e;
		if ( key.empty() ) key = message.getHeaders().getSingleOrEmpty( std::string { HY_ACCESS_KEY_HEADER_NAME } );

		if ( key.empty() )
			co_return hyAuthError( 401, "No access key or session key provided!", "MissingCredentialsException" );

		const auto rejection { co_await verifyApiKey( key, db ) };
		if ( rejection )
			co_return hyAuthError( 403, "Provided access key is invalid!", "InsufficientCredentialsException" );

		co_return std::nullopt;
#endif
	};
}

[[nodiscard]] static http::Response hyResponse(
	simdjson::builder::string_builder& builder,
	const std::uint16_t status = 200 )
{
	builder.append_comma();
	builder.append_key_value( "version", HYDRUS_MIMICED_API_VERSION );
	builder.append_comma();
	builder.append_key_value( "hydrus_version", HYDRUS_MIMICED_VERSION );
	builder.end_object();

	auto json { builder.view() };
	if ( json.error() ) throw std::runtime_error( simdjson::error_message( json.error() ) );

	return http::Response::json( status, folly::IOBuf::copyBuffer( *json ) );
}

[[nodiscard]] static http::Response hyUnsupportedResponse()
{
	simdjson::builder::string_builder builder;
	builder.start_object();
	builder.append_key_value( "status", 410 );
	builder.append_comma();
	builder.append_key_value( "message", "IDHAN Hydrus API does not support this request" );
	builder.end_object();

	auto json { builder.view() };
	if ( json.error() ) throw std::runtime_error( simdjson::error_message( json.error() ) );

	return http::Response::json( 410, folly::IOBuf::copyBuffer( *json ) );
}

[[nodiscard]] static http::Response hyNotImplementedResponse( const std::string_view what )
{
	return http::Response::error( 501, std::format( "IDHAN Hydrus API does not yet implement {}", what ) );
}

static http::HTTPTask< http::Response > apiVersion(
	[[maybe_unused]] db::DBContext& db,
	[[maybe_unused]] http::RequestContext context )
{
	simdjson::builder::string_builder builder;
	builder.start_object();
	builder.append_key_value( "is_idhan_instance", true );
	builder.append_comma();
	builder.append_key_value( "idhan_api_version", static_cast< std::uint64_t >( IDHAN_API_VERSION ) );
	co_return hyResponse( builder );
}

static http::HTTPTask< http::Response > verifyAccessKey(
	[[maybe_unused]] db::DBContext& db,
	[[maybe_unused]] http::RequestContext context )
{
	simdjson::builder::string_builder builder;
	builder.start_object();
	builder.append_key_value( "name", "IDHAN" );
	builder.append_comma();
	builder.append_key_value( "permits_everything", true );
	builder.append_comma();
	builder.escape_and_append_with_quotes( "basic_permissions" );
	builder.append_colon();
	builder.start_array();
	for ( int permission = 0; permission <= 13; ++permission )
	{
		if ( permission != 0 ) builder.append_comma();
		builder.append( permission );
	}
	builder.end_array();
	builder.append_comma();
	builder.append_key_value( "human_description", "This key has access to everything." );
	co_return hyResponse( builder );
}

static http::HTTPTask< http::Response > getService( db::DBContext& db, http::RequestContext context )
{
	const auto service_key_e { context.parameters().optional< std::string >( "service_key" ) };
	if ( !service_key_e ) co_return http::Response::error( 400, service_key_e.error().message );
	const auto service_name_e { context.parameters().optional< std::string >( "service_name" ) };
	if ( !service_name_e ) co_return http::Response::error( 400, service_name_e.error().message );

	if ( !*service_key_e && !*service_name_e )
		co_return http::Response::error( 400, "Must provide a service_key or service_name" );

	const auto services { co_await hyapi::listServices( db ) };

	std::optional< hyapi::ServiceInfo > found {};

	if ( *service_key_e )
	{
		found = hyapi::findServiceByKey( services, **service_key_e );
		if ( !found )
			co_return http::Response::error(
				404, std::format( "Did not find a service with key \"{}\"", **service_key_e ) );
	}
	else
	{
		found = hyapi::findServiceByName( services, **service_name_e );
		if ( !found )
			co_return http::Response::error(
				404, std::format( "Did not find a service with name \"{}\"", **service_name_e ) );
	}

	Json::Value root {};
	root[ "service" ] = hyapi::servicesList( std::span( &found.value(), 1 ) )[ 0 ];
	root[ "version" ] = static_cast< Json::UInt64 >( HYDRUS_MIMICED_API_VERSION );
	root[ "hydrus_version" ] = static_cast< Json::UInt64 >( HYDRUS_MIMICED_VERSION );

	co_return http::Response::json( 200, folly::IOBuf::copyBuffer( root.toStyledString() ) );
}

static http::HTTPTask< http::Response > getServices( db::DBContext& db, [[maybe_unused]] http::RequestContext context )
{
	const auto services { co_await hyapi::listServices( db ) };

	Json::Value root {};
	root[ "local_tags" ] = hyapi::servicesOfType( services, hydrus::gen_constants::LOCAL_TAG );
	root[ "tag_repositories" ] = hyapi::servicesOfType( services, hydrus::gen_constants::TAG_REPOSITORY );
	root[ "local_files" ] = hyapi::servicesOfType( services, hydrus::gen_constants::LOCAL_FILE_DOMAIN );
	root[ "local_updates" ] = hyapi::servicesOfType( services, hydrus::gen_constants::LOCAL_FILE_UPDATE_DOMAIN );
	root[ "file_repositories" ] = hyapi::servicesOfType( services, hydrus::gen_constants::FILE_REPOSITORY );
	root[ "all_local_files" ] = hyapi::servicesOfType( services, hydrus::gen_constants::HYDRUS_LOCAL_FILE_STORAGE );
	root[ "all_local_media" ] = hyapi::servicesOfType( services, hydrus::gen_constants::COMBINED_LOCAL_FILE_DOMAINS );
	root[ "all_known_files" ] = hyapi::servicesOfType( services, hydrus::gen_constants::COMBINED_FILE );
	root[ "all_known_tags" ] = hyapi::servicesOfType( services, hydrus::gen_constants::COMBINED_TAG );
	root[ "trash" ] = hyapi::servicesOfType( services, hydrus::gen_constants::LOCAL_FILE_TRASH_DOMAIN );

	root[ "services" ] = hyapi::servicesDict( services );
	root[ "services_v2" ] = hyapi::servicesList( services );

	root[ "version" ] = static_cast< Json::UInt64 >( HYDRUS_MIMICED_API_VERSION );
	root[ "hydrus_version" ] = static_cast< Json::UInt64 >( HYDRUS_MIMICED_VERSION );

	co_return http::Response::json( 200, folly::IOBuf::copyBuffer( root.toStyledString() ) );
}

struct BootInfo
{
	std::string boot_id;
	double boot_time;
};

[[nodiscard]] static const BootInfo& getBootInfo()
{
	static const BootInfo info {
		[]
		{
			std::array< std::byte, SHA256::size() > bytes {};

			std::random_device rd {};
			std::mt19937_64 gen { rd() };
			std::uniform_int_distribution< std::uint32_t > dist { 0, 255 };

			for ( auto& byte : bytes ) byte = static_cast< std::byte >( dist( gen ) );

			const auto now { std::chrono::system_clock::now() };
			const double boot_time { std::chrono::duration< double >( now.time_since_epoch() ).count() };

			return BootInfo { SHA256::fromBuffer( bytes ).hex(), boot_time };
		}()
	};

	return info;
}

static http::HTTPTask< http::Response > clientInfo(
	[[maybe_unused]] db::DBContext& db,
	[[maybe_unused]] http::RequestContext context )
{
	const auto& boot_info { getBootInfo() };

	simdjson::builder::string_builder builder;
	builder.start_object();
	builder.append_key_value( "boot_id", boot_info.boot_id );
	builder.append_comma();
	builder.append_key_value( "boot_time", boot_info.boot_time );
	builder.append_comma();
	builder.append_key_value( "currently_idle", false );
	co_return hyResponse( builder );
}

static http::HTTPTask< http::Response > getPopups(
	[[maybe_unused]] db::DBContext& db,
	[[maybe_unused]] http::RequestContext context )
{
	simdjson::builder::string_builder builder;
	builder.start_object();
	builder.escape_and_append_with_quotes( "job_statuses" );
	builder.append_colon();
	builder.start_array();
	builder.end_array();
	co_return hyResponse( builder );
}

static http::HTTPTask< http::Response > getPages(
	[[maybe_unused]] db::DBContext& db,
	[[maybe_unused]] http::RequestContext context )
{
	simdjson::builder::string_builder builder;
	builder.start_object();
	builder.escape_and_append_with_quotes( "pages" );
	builder.append_colon();
	builder.start_array();
	builder.start_object();
	builder.append_key_value( "name", "IDHAN Fake page" );
	builder.append_comma();
	builder.append_key_value( "page_key", "696468616e2066616b65207061676520666f7220687964727573206170690000" );
	builder.append_comma();
	builder.append_key_value( "page_state", static_cast< std::uint64_t >( hydrus::gen_constants::PAGE_STATE_NORMAL ) );
	builder.append_comma();
	builder.append_key_value(
		"page_type", static_cast< std::uint64_t >( hydrus::gen_constants::PAGE_TYPE_PAGE_OF_PAGES ) );
	builder.append_comma();
	builder.append_key_value( "is_media_page", false );
	builder.append_comma();
	builder.append_key_value( "selected", true );
	builder.end_object();
	builder.end_array();
	co_return hyResponse( builder );
}

static http::HTTPTask< http::Response > getClientOptions(
	db::DBContext& db,
	[[maybe_unused]] http::RequestContext context )
{
	static constexpr std::string_view raw_json {
		R"({"old_options": {}, "options": {}, "services": {}, "version": 79, "hydrus_version": 617})"
	};

	Json::Value root {};
	Json::Reader reader;
	reader.parse( std::string { raw_json }, root );

	const auto services { co_await hyapi::listServices( db ) };
	root[ "services" ] = hyapi::servicesDict( services );

	root[ "version" ] = static_cast< Json::UInt64 >( HYDRUS_MIMICED_API_VERSION );
	root[ "hydrus_version" ] = static_cast< Json::UInt64 >( HYDRUS_MIMICED_VERSION );

	co_return http::Response::json( 200, folly::IOBuf::copyBuffer( root.toStyledString() ) );
}

static http::HTTPTask< http::Response > searchTags( [[maybe_unused]] db::DBContext& db, http::RequestContext context )
{
	const auto search_e { context.parameters().optional< std::string >( "search" ) };
	if ( !search_e ) co_return http::Response::error( 400, search_e.error().message );
	if ( !*search_e ) co_return http::Response::error( 400, "Must provide search string" );

	const auto search_value { search_e->value_or( "" ) };

	if ( search_value.empty() )
	{
		simdjson::builder::string_builder builder;
		builder.start_object();
		builder.escape_and_append_with_quotes( "autocomplete_text" );
		builder.append_colon();
		builder.start_object();
		builder.append_key_value( "search_text", search_value );
		builder.append_comma();
		builder.append_key_value( "inclusive", true );
		builder.end_object();
		builder.append_comma();
		builder.escape_and_append_with_quotes( "tags" );
		builder.append_colon();
		builder.start_array();
		builder.end_array();
		co_return hyResponse( builder );
	}

	const auto display_type_e { context.parameters().optional< std::string >( "tag_display_type" ) };
	if ( !display_type_e ) co_return http::Response::error( 400, display_type_e.error().message );
	const auto display_type { display_type_e->value_or( "storage" ) };
	if ( display_type != "storage" && display_type != "display" )
		co_return http::Response::error( 400, "Invalid tag display type" );

	const auto results { co_await getSimilarTags( db, search_value, 10, true ) };

	simdjson::builder::string_builder builder;
	builder.start_object();
	builder.escape_and_append_with_quotes( "autocomplete_text" );
	builder.append_colon();
	builder.start_object();
	builder.append_key_value( "search_text", search_value );
	builder.append_comma();
	builder.append_key_value( "inclusive", !search_value.starts_with( '-' ) );
	builder.end_object();
	builder.append_comma();
	builder.escape_and_append_with_quotes( "tags" );
	builder.append_colon();
	builder.start_array();
	bool first { true };
	for ( const auto& tag : results )
	{
		if ( !first ) builder.append_comma();
		first = false;
		builder.start_object();
		builder.append_key_value( "value", tag.tag_text );
		builder.append_comma();
		builder.append_key_value( "count", tag.count );
		builder.end_object();
	}
	builder.end_array();
	co_return hyResponse( builder );
}

static http::HTTPTask< http::Response > searchFiles( db::DBContext& db, http::RequestContext context )
{
	const auto tags_e { context.parameters().optional< std::string >( "tags" ) };
	if ( !tags_e ) co_return http::Response::error( 400, tags_e.error().message );

	constexpr std::string_view empty_tags { "[]" };
	const auto tags_parameter { tags_e->value_or( std::string { empty_tags } ) };

	if ( tags_parameter == empty_tags )
	{
		simdjson::builder::string_builder builder;
		builder.start_object();
		builder.escape_and_append_with_quotes( "file_ids" );
		builder.append_colon();
		builder.start_array();
		builder.end_array();
		co_return hyResponse( builder );
	}

	simdjson::dom::parser parser;
	simdjson::dom::array tags_json {};
	try
	{
		if ( parser.parse( tags_parameter ).get( tags_json ) != simdjson::SUCCESS )
			co_return http::Response::error( 400, std::format( "Invalid tags json: Was {}", tags_parameter ) );
	}
	catch ( const simdjson::simdjson_error& )
	{
		co_return http::Response::error( 400, std::format( "Invalid tags json: Was {}", tags_parameter ) );
	}

	std::vector< std::string > search_tags {};
	std::vector< std::string > system_tags {};

	for ( const simdjson::dom::element tag : tags_json )
	{
		std::string_view tag_view {};
		if ( tag.get_string().get( tag_view ) != simdjson::SUCCESS )
			co_return http::Response::error( 400, "tags must be an array of strings" );

		std::string tag_text { tag_view };
		if ( tag_text.starts_with( "system:" ) )
			system_tags.emplace_back( std::move( tag_text ) );
		else
			search_tags.emplace_back( std::move( tag_text ) );
	}

	SearchQuery query {};

	auto tag_result_error { co_await query.setTags( db, search_tags ) };
	if ( tag_result_error ) co_return std::move( *tag_result_error );

	try
	{
		query.setSystemTags( system_tags );
	}
	catch ( const std::invalid_argument& e )
	{
		co_return http::Response::error( 400, std::format( "Invalid system tag: {}", e.what() ) );
	}

	const auto file_sort_type_e { context.parameters().optional< std::uint64_t >( "file_sort_type" ) };
	if ( !file_sort_type_e ) co_return http::Response::error( 400, file_sort_type_e.error().message );
	const auto file_sort_type {
		static_cast< HydrusSortType >( file_sort_type_e->value_or( HydrusSortType::DEFAULT ) )
	};

	const auto file_sort_asc_e { context.parameters().optional< bool >( "file_sort_asc" ) };
	if ( !file_sort_asc_e ) co_return http::Response::error( 400, file_sort_asc_e.error().message );
	const auto file_sort_asc { file_sort_asc_e->value_or( true ) };

	query.setSortType( hyToIDHANSortType( file_sort_type ) );
	query.setSortOrder( file_sort_asc ? SortOrder::ASC : SortOrder::DESC );

	const auto return_file_ids_e { context.parameters().optional< bool >( "return_file_ids" ) };
	if ( !return_file_ids_e ) co_return http::Response::error( 400, return_file_ids_e.error().message );
	const auto return_file_ids { return_file_ids_e->value_or( true ) };

	const auto return_hashes_e { context.parameters().optional< bool >( "return_hashes" ) };
	if ( !return_hashes_e ) co_return http::Response::error( 400, return_hashes_e.error().message );
	const auto return_hashes { return_hashes_e->value_or( false ) };

	const auto tag_display_type_e { context.parameters().optional< std::string >( "tag_display_type" ) };
	if ( !tag_display_type_e ) co_return http::Response::error( 400, tag_display_type_e.error().message );
	const auto tag_display_type { tag_display_type_e->value_or( "display" ) };

	query.setDisplay( tag_display_type == "storage" ? HydrusDisplayType::STORED : HydrusDisplayType::DISPLAY );

	const auto result { co_await query.query( db, {}, return_file_ids, return_hashes ) };

	simdjson::builder::string_builder builder;
	builder.start_object();
	bool first_field { true };
	const auto nextField = [ &builder, &first_field ]
	{
		if ( !first_field ) builder.append_comma();
		first_field = false;
	};

	if ( return_file_ids )
	{
		nextField();
		builder.escape_and_append_with_quotes( "file_ids" );
		builder.append_colon();
		builder.start_array();
		bool first_id { true };
		for ( const auto id : result.record_ids )
		{
			if ( !first_id ) builder.append_comma();
			first_id = false;
			builder.append( id );
		}
		builder.end_array();
	}

	if ( return_hashes )
	{
		nextField();
		builder.escape_and_append_with_quotes( "hashes" );
		builder.append_colon();
		builder.start_array();
		bool first_hash { true };
		for ( const auto& hash : result.hashes )
		{
			if ( !first_hash ) builder.append_comma();
			first_hash = false;
			builder.append( hash.hex() );
		}
		builder.end_array();
	}

	if ( first_field )
	{
		nextField();
		builder.escape_and_append_with_quotes( "file_ids" );
		builder.append_colon();
		builder.start_array();
		builder.end_array();
	}

	co_return hyResponse( builder );
}

[[nodiscard]] static http::HTTPTask< std::expected< std::vector< RecordID >, http::Response > >
	extractRecordIDsFromFilesJson( db::DBContext& db, const simdjson::dom::element json )
{
	if ( !json.is_object() )
		co_return std::unexpected( http::Response::error( 400, "Invalid JSON, Expected to be a object" ) );

	std::vector< RecordID > records {};

	std::string_view hash {};
	if ( json[ "hash" ].get( hash ) == simdjson::SUCCESS )
	{
		const auto sha256 { SHA256::fromHex( std::string { hash } ) };
		if ( !sha256 ) co_return std::unexpected( http::Response::error( 400, "Invalid SHA256" ) );
		const auto record_id { co_await findRecord( *sha256, db ) };
		if ( !record_id ) co_return std::unexpected( http::Response::error( 400, "Invalid SHA256" ) );
		records.emplace_back( *record_id );
		co_return records;
	}

	simdjson::dom::array hashes {};
	if ( json[ "hashes" ].get( hashes ) == simdjson::SUCCESS )
	{
		for ( const simdjson::dom::element entry : hashes )
		{
			std::string_view entry_hash {};
			if ( entry.get_string().get( entry_hash ) != simdjson::SUCCESS )
				co_return std::unexpected( http::Response::error( 400, "Invalid SHA256" ) );
			const auto sha256 { SHA256::fromHex( std::string { entry_hash } ) };
			if ( !sha256 ) co_return std::unexpected( http::Response::error( 400, "Invalid SHA256" ) );
			const auto record_id { co_await findRecord( *sha256, db ) };
			if ( !record_id ) co_return std::unexpected( http::Response::error( 400, "Invalid SHA256" ) );
			records.emplace_back( *record_id );
		}
		co_return records;
	}

	std::int64_t record_id_value {};
	if ( json[ "record_id" ].get( record_id_value ) == simdjson::SUCCESS )
	{
		records.emplace_back( static_cast< RecordID >( record_id_value ) );
		co_return records;
	}

	simdjson::dom::array record_ids {};
	if ( json[ "record_ids" ].get( record_ids ) == simdjson::SUCCESS )
	{
		for ( const simdjson::dom::element entry : record_ids )
		{
			std::int64_t id {};
			if ( entry.get_int64().get( id ) != simdjson::SUCCESS )
				co_return std::unexpected( http::Response::error( 400, "record_ids must be an array of integers" ) );
			records.emplace_back( static_cast< RecordID >( id ) );
		}
		co_return records;
	}

	co_return std::unexpected(
		http::Response::error(
			400, "Unable to extract record ids: None of the expected hydrus `files` json members were identified" ) );
}

[[nodiscard]] static http::HTTPTask< std::expected< UrlID, http::Response > > findOrCreateHydrusUrl(
	db::DBContext& db,
	const std::string& url )
{
	db::DBBindings find_bindings {};
	find_bindings.bind( url );
	const auto existing {
		co_await db.query( "SELECT url_id FROM urls WHERE url = $1", std::move( find_bindings ) ).rows< UrlID >()
	};
	if ( !existing.empty() ) co_return std::get< 0 >( existing.row( 0 ) );

	const auto domain { helpers::extractDomain( url ) };

	db::DBBindings domain_bindings {};
	domain_bindings.bind( domain );
	co_await db
		.query(
			"INSERT INTO url_domains (url_domain) VALUES ($1) ON CONFLICT (url_domain) DO NOTHING",
			std::move( domain_bindings ) )
		.result();

	db::DBBindings domain_lookup_bindings {};
	domain_lookup_bindings.bind( domain );
	const auto domain_rows {
		co_await db
			.query( "SELECT url_domain_id FROM url_domains WHERE url_domain = $1", std::move( domain_lookup_bindings ) )
			.rows< UrlDomainID >()
	};
	if ( domain_rows.empty() )
		co_return std::unexpected(
			http::Response::error( 500, std::format( "Failed to create URL domain for {}", url ) ) );

	const auto url_domain_id { std::get< 0 >( domain_rows.row( 0 ) ) };

	db::DBBindings insert_bindings {};
	insert_bindings.bind( url );
	insert_bindings.bind( url_domain_id );
	const auto inserted {
		co_await db
			.query(
				"INSERT INTO urls (url, url_domain_id) VALUES ($1, $2) ON CONFLICT DO NOTHING RETURNING url_id",
				std::move( insert_bindings ) )
			.rows< UrlID >()
	};
	if ( !inserted.empty() ) co_return std::get< 0 >( inserted.row( 0 ) );

	db::DBBindings retry_bindings {};
	retry_bindings.bind( url );
	const auto retry {
		co_await db.query( "SELECT url_id FROM urls WHERE url = $1", std::move( retry_bindings ) ).rows< UrlID >()
	};
	if ( !retry.empty() ) co_return std::get< 0 >( retry.row( 0 ) );

	co_return std::unexpected(
		http::Response::error( 500, std::format( "Was unable to create or find url {}", url ) ) );
}

static http::HTTPTask< http::Response > associateUrl( db::DBContext& db, http::RequestContext context )
{
	auto body_e { readValidatedJsonBody( context ) };
	if ( !body_e ) co_return std::move( body_e.error() );

	simdjson::dom::parser parser;
	simdjson::dom::element root {};
	try
	{
		root = parser.parse( *body_e );
	}
	catch ( const simdjson::simdjson_error& e )
	{
		co_return http::Response::error( 400, e.what() );
	}

	auto records_e { co_await extractRecordIDsFromFilesJson( db, root ) };
	if ( !records_e ) co_return std::move( records_e.error() );
	const auto& records { *records_e };

	std::vector< std::string > urls_to_add {};
	std::vector< std::string > urls_to_delete {};

	std::string_view single_add {};
	if ( root[ "url_to_add" ].get( single_add ) == simdjson::SUCCESS ) urls_to_add.emplace_back( single_add );

	std::string_view single_delete {};
	if ( root[ "url_to_delete" ].get( single_delete ) == simdjson::SUCCESS )
		urls_to_delete.emplace_back( single_delete );

	simdjson::dom::array add_array {};
	if ( root[ "urls_to_add" ].get( add_array ) == simdjson::SUCCESS )
	{
		for ( const simdjson::dom::element entry : add_array )
		{
			std::string_view url {};
			if ( entry.get_string().get( url ) != simdjson::SUCCESS )
				co_return http::Response::error( 400, "urls_to_add must be an array of strings" );
			urls_to_add.emplace_back( url );
		}
	}

	simdjson::dom::array delete_array {};
	if ( root[ "urls_to_delete" ].get( delete_array ) == simdjson::SUCCESS )
	{
		for ( const simdjson::dom::element entry : delete_array )
		{
			std::string_view url {};
			if ( entry.get_string().get( url ) != simdjson::SUCCESS )
				co_return http::Response::error( 400, "urls_to_delete must be an array of strings" );
			urls_to_delete.emplace_back( url );
		}
	}

	if ( urls_to_add.empty() && urls_to_delete.empty() )
		co_return http::Response::error( 400, "Did not find any URLs to add or delete" );

	for ( const auto& url : urls_to_add )
	{
		auto url_id_e { co_await findOrCreateHydrusUrl( db, url ) };
		if ( !url_id_e ) co_return std::move( url_id_e.error() );

		for ( const auto record_id : records )
		{
			db::DBBindings bindings {};
			bindings.bind( record_id );
			bindings.bind( *url_id_e );
			co_await db
				.query(
					"INSERT INTO url_mappings (record_id, url_id) VALUES ($1, $2) ON CONFLICT DO NOTHING",
					std::move( bindings ) )
				.result();
		}
	}

	for ( const auto& url : urls_to_delete )
	{
		auto url_id_e { co_await findOrCreateHydrusUrl( db, url ) };
		if ( !url_id_e ) co_return std::move( url_id_e.error() );

		for ( const auto record_id : records )
		{
			db::DBBindings bindings {};
			bindings.bind( record_id );
			bindings.bind( *url_id_e );
			co_await db.query( "DELETE FROM url_mappings WHERE record_id = $1 AND url_id = $2", std::move( bindings ) )
				.result();
		}
	}

	co_return http::Response::json( 200, folly::IOBuf::copyBuffer( std::string_view { "{}" } ) );
}

static http::HTTPTask< http::Response > getUrlInfo( [[maybe_unused]] db::DBContext& db, http::RequestContext context )
{
	const auto url_e { context.parameters().optional< std::string >( "url" ) };
	if ( !url_e ) co_return http::Response::error( 400, url_e.error().message );
	if ( !*url_e || ( *url_e )->empty() ) co_return http::Response::error( 400, "Must provide url parameter" );

	const auto& url_str { **url_e };

	simdjson::builder::string_builder builder;
	builder.start_object();
	builder.append_key_value( "request_url", url_str );
	builder.append_comma();
	builder.append_key_value( "normalised_url", url_str );
	builder.append_comma();
	builder.append_key_value( "url_type", static_cast< std::uint64_t >( hydrus::gen_constants::URL_TYPE_UNKNOWN ) );
	builder.append_comma();
	builder.append_key_value(
		"url_type_string", std::string { hyapi::urlTypeString( hydrus::gen_constants::URL_TYPE_UNKNOWN ) } );
	builder.append_comma();
	builder.append_key_value( "match_name", "unknown url" );
	builder.append_comma();
	builder.append_key_value( "can_parse", false );
	builder.append_comma();
	builder.append_key_value( "cannot_parse_reason", "unknown url class" );
	co_return hyResponse( builder );
}

[[nodiscard]] static http::HTTPTask< std::expected< RecordID, http::Response > > resolveHydrusFileID(
	db::DBContext& db,
	http::RequestContext& context )
{
	const auto file_id_e { context.parameters().optional< RecordID >( "file_id" ) };
	if ( !file_id_e ) co_return std::unexpected( http::Response::error( 400, file_id_e.error().message ) );

	const auto hash_e { context.parameters().optional< std::string >( "hash" ) };
	if ( !hash_e ) co_return std::unexpected( http::Response::error( 400, hash_e.error().message ) );

	if ( *hash_e )
	{
		const auto sha256 { SHA256::fromHex( **hash_e ) };
		if ( !sha256 ) co_return std::unexpected( http::Response::error( 400, "Invalid hash" ) );

		const auto record_id { co_await findRecord( *sha256, db ) };
		if ( !record_id )
			co_return std::unexpected(
				http::Response::error( 404, std::format( "No record with hash {} found", **hash_e ) ) );

		co_return *record_id;
	}

	if ( !*file_id_e ) co_return std::unexpected( http::Response::error( 400, "No hash or file_id specified" ) );

	co_return **file_id_e;
}

static http::HTTPTask< http::Response > file( db::DBContext& db, http::RequestContext context )
{
	auto record_id_e { co_await resolveHydrusFileID( db, context ) };
	if ( !record_id_e ) co_return std::move( record_id_e.error() );

	co_return co_await recordFileResponse( db, context, *record_id_e );
}

static http::HTTPTask< http::Response > thumbnail( db::DBContext& db, http::RequestContext context )
{
	auto record_id_e { co_await resolveHydrusFileID( db, context ) };
	if ( !record_id_e ) co_return std::move( record_id_e.error() );

	co_return co_await recordThumbnailResponse( db, context, *record_id_e );
}

static http::HTTPTask< http::Response > unsupported(
	[[maybe_unused]] db::DBContext& db,
	[[maybe_unused]] http::RequestContext context )
{
	co_return hyUnsupportedResponse();
}

static http::HTTPTask< http::Response > requestNewPermissions(
	[[maybe_unused]] db::DBContext& db,
	[[maybe_unused]] http::RequestContext context )
{
	co_return hyNotImplementedResponse( "request_new_permissions" );
}

static http::HTTPTask< http::Response > sessionKey(
	[[maybe_unused]] db::DBContext& db,
	[[maybe_unused]] http::RequestContext context )
{
	co_return hyNotImplementedResponse( "session_key" );
}

static http::HTTPTask< http::Response > addFile(
	[[maybe_unused]] db::DBContext& db,
	[[maybe_unused]] http::RequestContext context )
{
	co_return hyNotImplementedResponse( "add_files/add_file" );
}

static http::HTTPTask< http::Response > fileHashes(
	[[maybe_unused]] db::DBContext& db,
	[[maybe_unused]] http::RequestContext context )
{
	co_return hyNotImplementedResponse( "get_files/file_hashes" );
}

std::vector< http::RouteDefinition > hydrusRoutes( db::DBContext& db )
{
	std::vector< http::RouteDefinition > routes {};

	const auto add =
		[ &routes, &db ](
			const proxygen::HTTPMethod method,
			std::string path,
			const bool needs_auth,
			http::HTTPTask< http::Response > ( *handler )( db::DBContext&, http::RequestContext ) )
	{
		std::vector< http::RequestValidation > before_body {};
		if ( needs_auth ) before_body.push_back( hyAuthValidation( db ) );

		auto bound = [ &db, handler ]( http::RequestContext context ) -> http::HTTPTask< http::Response >
		{ co_return co_await handler( db, std::move( context ) ); };

		routes.push_back(
			http::RouteDefinition {
				http::RouteMetadata { method, std::move( path ), {}, std::move( before_body ) }, std::move( bound ) } );
	};

	add( proxygen::HTTPMethod::GET, "/hyapi/api_version", false, apiVersion );
	add( proxygen::HTTPMethod::GET, "/hyapi/request_new_permissions", true, requestNewPermissions );
	add( proxygen::HTTPMethod::GET, "/hyapi/session_key", true, sessionKey );
	add( proxygen::HTTPMethod::GET, "/hyapi/verify_access_key", true, verifyAccessKey );
	add( proxygen::HTTPMethod::GET, "/hyapi/get_service", true, getService );
	add( proxygen::HTTPMethod::GET, "/hyapi/get_services", true, getServices );
	add( proxygen::HTTPMethod::GET, "/hyapi/client_info", true, clientInfo );

	add( proxygen::HTTPMethod::GET, "/hyapi/add_files/add_file", true, addFile );

	add( proxygen::HTTPMethod::GET, "/hyapi/get_files/search_files", true, searchFiles );
	add( proxygen::HTTPMethod::GET, "/hyapi/get_files/file_hashes", true, fileHashes );
	add( proxygen::HTTPMethod::GET, "/hyapi/get_files/file", true, file );
	add( proxygen::HTTPMethod::GET, "/hyapi/get_files/thumbnail", true, thumbnail );
	add( proxygen::HTTPMethod::GET, "/hyapi/get_files/file_path", true, unsupported );
	add( proxygen::HTTPMethod::GET, "/hyapi/get_files/thumbnail_path", true, unsupported );

	add( proxygen::HTTPMethod::POST, "/hyapi/add_urls/add_url", true, unsupported );
	add( proxygen::HTTPMethod::POST, "/hyapi/add_urls/associate_url", true, associateUrl );
	add( proxygen::HTTPMethod::GET, "/hyapi/add_urls/get_url_info", true, getUrlInfo );

	add( proxygen::HTTPMethod::GET, "/hyapi/add_tags/search_tags", true, searchTags );
	add( proxygen::HTTPMethod::GET, "/hyapi/manage_database/get_client_options", true, getClientOptions );

	add( proxygen::HTTPMethod::GET, "/hyapi/manage_popups/get_popups", true, getPopups );
	add( proxygen::HTTPMethod::GET, "/hyapi/manage_pages/get_pages", true, getPages );

	return routes;
}

} // namespace idhan::httpapi
