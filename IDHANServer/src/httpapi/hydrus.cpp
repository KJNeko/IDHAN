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
#include "db/fileInfo.hpp"
#include "downloadSessions.hpp"
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
#include "urls/UrlNormalization.hpp"
#include "IDHANDownloader/urls/UrlNormalization.hpp"

namespace idhan::httpapi
{

#ifndef IDHAN_DISABLE_API_AUTH
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
#endif

[[nodiscard]] static http::RequestValidation hyAuthValidation( db::DBContext& db )
{
	return [ &db ]( const proxygen::HTTPMessage& message, const http::RequestParameters& parameters )
	           -> http::HTTPTask< std::optional< http::Response > >
	{
#ifdef IDHAN_DISABLE_API_AUTH
		(void)message;
		(void)parameters;
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

static http::HTTPTask< http::Response > getService( db::DBContext& db, http::Query< HydrusServiceQuery > parameters )
{
	if ( !parameters.value.service_key && !parameters.value.service_name )
		co_return http::Response::error( 400, "Must provide a service_key or service_name" );

	const auto services { co_await hyapi::listServices( db ) };

	std::optional< hyapi::ServiceInfo > found {};

	if ( parameters.value.service_key )
	{
		found = hyapi::findServiceByKey( services, *parameters.value.service_key );
		if ( !found )
			co_return http::Response::error(
				404, std::format( "Did not find a service with key \"{}\"", *parameters.value.service_key ) );
	}
	else
	{
		found = hyapi::findServiceByName( services, *parameters.value.service_name );
		if ( !found )
			co_return http::Response::error(
				404, std::format( "Did not find a service with name \"{}\"", *parameters.value.service_name ) );
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

static http::HTTPTask< http::Response > searchTags(
	[[maybe_unused]] db::DBContext& db,
	http::Query< HydrusTagSearchQuery > parameters )
{
	if ( !parameters.value.search ) co_return http::Response::error( 400, "Must provide search string" );

	const auto search_value { parameters.value.search.value_or( "" ) };

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

	const auto display_type { parameters.value.tag_display_type.value_or( "storage" ) };
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

static http::HTTPTask< http::Response > searchFiles(
	db::DBContext& db,
	http::Query< HydrusFileSearchQuery > parameters )
{
	constexpr std::string_view empty_tags { "[]" };
	const auto tags_text { parameters.value.tags.value_or( std::string { empty_tags } ) };

	if ( tags_text == empty_tags )
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
		if ( parser.parse( tags_text ).get( tags_json ) != simdjson::SUCCESS )
			co_return http::Response::error( 400, std::format( "Invalid tags json: Was {}", tags_text ) );
	}
	catch ( const simdjson::simdjson_error& )
	{
		co_return http::Response::error( 400, std::format( "Invalid tags json: Was {}", tags_text ) );
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

	const auto file_sort_type {
		static_cast< HydrusSortType >( parameters.value.file_sort_type.value_or( HydrusSortType::DEFAULT ) )
	};

	const auto file_sort_asc { parameters.value.file_sort_asc.value_or( true ) };

	query.setSortType( hyToIDHANSortType( file_sort_type ) );
	query.setSortOrder( file_sort_asc ? SortOrder::ASC : SortOrder::DESC );

	const auto return_file_ids { parameters.value.return_file_ids.value_or( true ) };

	const auto return_hashes { parameters.value.return_hashes.value_or( false ) };

	const auto tag_display_type { parameters.value.tag_display_type.value_or( "display" ) };

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
		const auto sha256 { SHA256::fromHex( hash ) };
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
			const auto sha256 { SHA256::fromHex( entry_hash ) };
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
		co_await db
				 .query(
					 //language=postgresql
					 R"SQL(
					SELECT
					    url_id
					FROM urls
					WHERE url = $1
				)SQL",
					 std::move( find_bindings ) )
				 .rows< UrlID >()
	};
	if ( !existing.empty() ) co_return std::get< 0 >( existing.row( 0 ) );

	const auto domain { helpers::extractDomain( url ) };

	db::DBBindings domain_bindings {};
	domain_bindings.bind( domain );
	co_await db
		.query(
			//language=postgresql
			R"SQL(
				INSERT INTO url_domains (url_domain)
				    VALUES ($1)
				ON CONFLICT (url_domain)
				    DO NOTHING
			)SQL",
			std::move( domain_bindings ) )
		.result();

	db::DBBindings domain_lookup_bindings {};
	domain_lookup_bindings.bind( domain );
	const auto domain_rows {
		co_await db
				 .query(
					 //language=postgresql
					 R"SQL(
					SELECT
					    url_domain_id
					FROM url_domains
					WHERE url_domain = $1
				)SQL",
					 std::move( domain_lookup_bindings ) )
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
				//language=postgresql
				R"SQL(
					INSERT INTO urls (url, url_domain_id)
					    VALUES ($1, $2)
					ON CONFLICT
					    DO NOTHING
					RETURNING url_id
				)SQL",
				std::move( insert_bindings ) )
			.rows< UrlID >()
	};
	if ( !inserted.empty() ) co_return std::get< 0 >( inserted.row( 0 ) );

	db::DBBindings retry_bindings {};
	retry_bindings.bind( url );
	const auto retry {
		co_await db
				 .query(
					 //language=postgresql
					 R"SQL(
					SELECT
					    url_id
					FROM urls
					WHERE url = $1
				)SQL",
					 std::move( retry_bindings ) )
				 .rows< UrlID >()
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

	auto normalized_add_e { co_await helpers::normalizeUrls( std::move( urls_to_add ) ) };
	if ( !normalized_add_e.has_value() )
	{
		const auto& error { normalized_add_e.error() };
		co_return http::Response::error( helpers::httpStatus( error ), error.message );
	}

	auto delete_spellings_e { co_await helpers::urlSpellings( std::move( urls_to_delete ) ) };
	if ( !delete_spellings_e.has_value() )
	{
		const auto& error { delete_spellings_e.error() };
		co_return http::Response::error( helpers::httpStatus( error ), error.message );
	}

	for ( const auto& url : normalized_add_e.value() )
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
					//language=postgresql
					R"SQL(
						INSERT INTO url_mappings (record_id, url_id)
						    VALUES ($1, $2)
						ON CONFLICT
						    DO NOTHING
					)SQL",
					std::move( bindings ) )
				.result();
		}
	}

	auto& delete_spellings { delete_spellings_e.value() };
	if ( !delete_spellings.empty() )
	{
		db::DBBindings bindings {};
		bindings.bind( std::move( delete_spellings ) );
		bindings.bind( records );
		co_await db
			.query(
				//language=postgresql
				R"SQL(
					DELETE FROM url_mappings m USING urls u
					WHERE u.url = ANY ($1::text[])
					    AND m.url_id = u.url_id
					    AND m.record_id = ANY ($2::integer[])
				)SQL",
				std::move( bindings ) )
			.result();
	}

	co_return http::Response::json( 200, folly::IOBuf::copyBuffer( std::string_view { "{}" } ) );
}

static http::HTTPTask< http::JsonResult< HydrusAddedUrlResponse > > addUrl(
	db::DBContext& db,
	http::JsonBody< HydrusAddUrlBody > body )
{
	const auto& url { body.value.url };
	if ( url.empty() || url.size() > 8192 ) co_return http::Response::error( 400, "url must be 1 to 8192 characters" );

	const auto parsed_e { downloader::parseUrl( url ) };
	if ( !parsed_e.has_value() ) co_return http::Response::error( 400, parsed_e.error() );

	const auto protocol { parsed_e.value().get_protocol() };
	if ( protocol != "http:" && protocol != "https:" )
		co_return http::Response::error( 400, "URL must use HTTP or HTTPS" );

	auto submitted { co_await submitUrlSession(
		db, http::JsonBody< SubmitUrlSessionBody > { { .name = {}, .url = url, .options = {} } } ) };
	auto* error { std::get_if< http::Response >( &submitted ) };
	if ( error ) co_return std::move( *error );

	co_return http::JsonResponse< HydrusAddedUrlResponse > { { "URL queued for download", url } };
}

static http::HTTPTask< http::JsonResult< HydrusUrlFilesResponse > > getUrlFiles(
	db::DBContext& db,
	http::Query< HydrusUrlInfoQuery > parameters )
{
	if ( !parameters.value.url ) co_return http::Response::error( 400, "Must provide url parameter" );

	const auto& url { *parameters.value.url };
	if ( url.empty() || url.size() > 8192 ) co_return http::Response::error( 400, "url must be 1 to 8192 characters" );

	auto normalized_e { co_await helpers::normalizeUrl( url ) };
	if ( !normalized_e.has_value() )
	{
		const auto& error { normalized_e.error() };
		co_return http::Response::error( helpers::httpStatus( error ), error.message );
	}

	auto& normalized { normalized_e.value() };
	const auto hashes { co_await db::storedRecordHashesForUrl( db, normalized ) };
	HydrusUrlFilesResponse response { .normalised_url = std::move( normalized ), .url_file_statuses = {} };
	for ( const auto& hash : hashes )
	{
		// Hydrus status 2 means the file is already imported.
		response.url_file_statuses.push_back( { 2, hash.hex(), "Already in the database" } );
	}
	co_return http::JsonResponse< HydrusUrlFilesResponse > { std::move( response ) };
}

static http::HTTPTask< http::Response > getUrlInfo(
	[[maybe_unused]] db::DBContext& db,
	http::Query< HydrusUrlInfoQuery > parameters )
{
	if ( !parameters.value.url || ( parameters.value.url )->empty() )
		co_return http::Response::error( 400, "Must provide url parameter" );

	const auto& url_str { *parameters.value.url };
	const auto normalized_e { co_await helpers::normalizeUrl( url_str ) };
	if ( !normalized_e.has_value() )
	{
		const auto& error { normalized_e.error() };
		co_return http::Response::error( helpers::httpStatus( error ), error.message );
	}

	simdjson::builder::string_builder builder;
	builder.start_object();
	builder.append_key_value( "request_url", url_str );
	builder.append_comma();
	builder.append_key_value( "normalised_url", normalized_e.value() );
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
	const HydrusFileQuery& parameters )
{
	const auto& file_id { parameters.file_id };

	const auto& hash { parameters.hash };

	if ( hash )
	{
		const auto sha256 { SHA256::fromHex( *hash ) };
		if ( !sha256 ) co_return std::unexpected( http::Response::error( 400, "Invalid hash" ) );

		const auto record_id { co_await findRecord( *sha256, db ) };
		if ( !record_id )
			co_return std::unexpected(
				http::Response::error( 404, std::format( "No record with hash {} found", *hash ) ) );

		co_return *record_id;
	}

	if ( !file_id ) co_return std::unexpected( http::Response::error( 400, "No hash or file_id specified" ) );

	co_return *file_id;
}

static http::HTTPTask< http::Response > file(
	db::DBContext& db,
	http::RequestContext context,
	http::Query< HydrusFileQuery > parameters )
{
	auto record_id_e { co_await resolveHydrusFileID( db, parameters.value ) };
	if ( !record_id_e ) co_return std::move( record_id_e.error() );

	co_return co_await recordFileResponse(
		db, context, *record_id_e, FetchRecordFileQuery { parameters.value.download } );
}

static http::HTTPTask< http::Response > thumbnail(
	db::DBContext& db,
	http::RequestContext context,
	http::Query< HydrusFileQuery > parameters )
{
	auto record_id_e { co_await resolveHydrusFileID( db, parameters.value ) };
	if ( !record_id_e ) co_return std::move( record_id_e.error() );

	co_return co_await recordThumbnailResponse(
		db, context, *record_id_e, FetchThumbnailQuery { parameters.value.regenerate, parameters.value.size } );
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
		[ &routes, &db ]< auto Handler >( const proxygen::HTTPMethod method, std::string path, const bool needs_auth )
	{
		std::vector< http::RequestValidation > before_body {};
		if ( needs_auth ) before_body.push_back( hyAuthValidation( db ) );

		using Arguments = typename http::HandlerArguments< decltype( Handler ) >::Tuple;
		auto bound = [ &db ]( http::RequestContext context )
		{ return http::invokeReflected< Handler, Arguments >( &db, std::move( context ) ); };

		routes.push_back(
			http::RouteDefinition {
				http::RouteMetadata { method, std::move( path ), {}, std::move( before_body ) }, std::move( bound ) } );
	};

	add.template operator()< &apiVersion >( proxygen::HTTPMethod::GET, "/hyapi/api_version", false );
	add.template
		operator()< &requestNewPermissions >( proxygen::HTTPMethod::GET, "/hyapi/request_new_permissions", true );
	add.template operator()< &sessionKey >( proxygen::HTTPMethod::GET, "/hyapi/session_key", true );
	add.template operator()< &verifyAccessKey >( proxygen::HTTPMethod::GET, "/hyapi/verify_access_key", true );
	add.template operator()< &getService >( proxygen::HTTPMethod::GET, "/hyapi/get_service", true );
	add.template operator()< &getServices >( proxygen::HTTPMethod::GET, "/hyapi/get_services", true );
	add.template operator()< &clientInfo >( proxygen::HTTPMethod::GET, "/hyapi/client_info", true );

	add.template operator()< &addFile >( proxygen::HTTPMethod::GET, "/hyapi/add_files/add_file", true );

	add.template operator()< &searchFiles >( proxygen::HTTPMethod::GET, "/hyapi/get_files/search_files", true );
	add.template operator()< &fileHashes >( proxygen::HTTPMethod::GET, "/hyapi/get_files/file_hashes", true );
	add.template operator()< &file >( proxygen::HTTPMethod::GET, "/hyapi/get_files/file", true );
	add.template operator()< &thumbnail >( proxygen::HTTPMethod::GET, "/hyapi/get_files/thumbnail", true );
	add.template operator()< &unsupported >( proxygen::HTTPMethod::GET, "/hyapi/get_files/file_path", true );
	add.template operator()< &unsupported >( proxygen::HTTPMethod::GET, "/hyapi/get_files/thumbnail_path", true );

	add.template operator()< &addUrl >( proxygen::HTTPMethod::POST, "/hyapi/add_urls/add_url", true );
	add.template operator()< &getUrlFiles >( proxygen::HTTPMethod::GET, "/hyapi/add_urls/get_url_files", true );
	add.template operator()< &associateUrl >( proxygen::HTTPMethod::POST, "/hyapi/add_urls/associate_url", true );
	add.template operator()< &getUrlInfo >( proxygen::HTTPMethod::GET, "/hyapi/add_urls/get_url_info", true );

	add.template operator()< &searchTags >( proxygen::HTTPMethod::GET, "/hyapi/add_tags/search_tags", true );
	add.template
		operator()< &getClientOptions >( proxygen::HTTPMethod::GET, "/hyapi/manage_database/get_client_options", true );

	add.template operator()< &getPopups >( proxygen::HTTPMethod::GET, "/hyapi/manage_popups/get_popups", true );
	add.template operator()< &getPages >( proxygen::HTTPMethod::GET, "/hyapi/manage_pages/get_pages", true );

	return routes;
}

} // namespace idhan::httpapi
