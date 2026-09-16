#include "search.hpp"

#include <folly/io/IOBuf.h>

#include <chrono>
#include <simdjson.h>
#include <string>
#include <vector>

#include "apiAuth.hpp"
#include "idhan/db/DBContext.hpp"
#include "jsonBody.hpp"
#include "logging/log.hpp"
#include "search/SearchQuery.hpp"
#include "search/parseSortType.hpp"

namespace idhan::httpapi
{

[[nodiscard]] static http::HTTPTask< http::Response > search( db::DBContext& db, http::RequestContext context )
{
	auto tag_ids_e { context.parameters().vector< TagID >( "tag_ids" ) };
	if ( !tag_ids_e ) co_return http::Response::error( 400, tag_ids_e.error().message );
	auto tag_ids { std::move( *tag_ids_e ) };

	if ( tag_ids.empty() )
	{
		auto tag_id_e { context.parameters().vector< TagID >( "tag_id" ) };
		if ( !tag_id_e ) co_return http::Response::error( 400, tag_id_e.error().message );
		tag_ids = std::move( *tag_id_e );
	}

	auto tag_domain_ids_e { context.parameters().vector< TagDomainID >( "tag_domains" ) };
	if ( !tag_domain_ids_e ) co_return http::Response::error( 400, tag_domain_ids_e.error().message );

	auto by_e { context.parameters().optional< std::string >( "by" ) };
	if ( !by_e ) co_return http::Response::error( 400, by_e.error().message );
	auto order_e { context.parameters().optional< std::string >( "order" ) };
	if ( !order_e ) co_return http::Response::error( 400, order_e.error().message );

	SearchQuery query {};
	query.addPositiveTags( tag_ids );

	if ( *by_e ) query.setSortType( parseSortType( **by_e ) );
	if ( *order_e ) query.setSortOrder( **order_e == "desc" ? SortOrder::DESC : SortOrder::ASC );

	const auto result { co_await query.query( db, std::move( *tag_domain_ids_e ) ) };

	simdjson::builder::string_builder builder;
	builder.start_array();
	bool first { true };
	for ( const auto id : result.record_ids )
	{
		if ( !first ) builder.append_comma();
		builder.append( id );
		first = false;
	}
	builder.end_array();

	auto json { builder.view() };
	if ( json.error() ) throw std::runtime_error( simdjson::error_message( json.error() ) );

	co_return http::Response::json( 200, folly::IOBuf::copyBuffer( *json ) );
}

struct SplitTags
{
	std::vector< std::string > text_tags {};
	std::vector< std::string > system_tags {};
	std::vector< std::string > wildcard_namespaces {};
	std::vector< std::string > wildcard_tags {};
};

[[nodiscard]] static SplitTags splitTags( const simdjson::dom::array& tags )
{
	SplitTags out {};

	for ( simdjson::dom::element tag_element : tags )
	{
		std::string_view tag_view {};
		if ( tag_element.get_string().get( tag_view ) != simdjson::SUCCESS )
			throw std::invalid_argument( "'tags' must be an array of strings" );

		std::string tag_text { tag_view };
		const bool is_namespace_wildcard { tag_text.ends_with( ":*" ) && tag_text.find( '*' ) == tag_text.size() - 1 };

		if ( tag_text.starts_with( "system:" ) )
			out.system_tags.emplace_back( std::move( tag_text ) );
		else if ( is_namespace_wildcard )
			out.wildcard_namespaces.emplace_back( std::move( tag_text ) );
		else if ( tag_text.contains( '*' ) )
			out.wildcard_tags.emplace_back( std::move( tag_text ) );
		else
			out.text_tags.emplace_back( std::move( tag_text ) );
	}

	return out;
}

[[nodiscard]] static http::HTTPTask< http::Response > searchPost( db::DBContext& db, http::RequestContext context )
{
	try
	{
		const auto start { std::chrono::steady_clock::now() };

		auto body_e { readValidatedJsonBody( context ) };
		if ( !body_e ) co_return std::move( body_e.error() );

		simdjson::dom::parser parser;
		const simdjson::dom::element root { parser.parse( *body_e ) };
		if ( !root.is_object() ) co_return http::Response::error( 400, "Request body must be a JSON object" );

		SearchQuery query {};

		simdjson::dom::array tags_array {};
		const auto tags_ec { root[ "tags" ].get( tags_array ) };
		if ( tags_ec != simdjson::NO_SUCH_FIELD )
		{
			if ( tags_ec != simdjson::SUCCESS )
				co_return http::Response::error( 400, "'tags' must be an array of strings" );

			const auto split { splitTags( tags_array ) };

			log::info(
				"Search got {} tags, {} system tags, {} wildcard namespaces, {} wildcard tags",
				split.text_tags.size(),
				split.system_tags.size(),
				split.wildcard_namespaces.size(),
				split.wildcard_tags.size() );

			auto tag_result_error { co_await query.setTags( db, split.text_tags ) };
			if ( tag_result_error ) co_return std::move( *tag_result_error );
			auto namespace_result_error { co_await query.setWildcardNamespaces( db, split.wildcard_namespaces ) };
			if ( namespace_result_error ) co_return std::move( *namespace_result_error );
			auto wildcard_result_error { co_await query.setWildcardTags( db, split.wildcard_tags ) };
			if ( wildcard_result_error ) co_return std::move( *wildcard_result_error );

			query.setSystemTags( split.system_tags );
		}
		else
		{
			simdjson::dom::array tag_ids_array {};
			const auto tag_ids_ec { root[ "tag_ids" ].get( tag_ids_array ) };
			if ( tag_ids_ec != simdjson::NO_SUCH_FIELD )
			{
				if ( tag_ids_ec != simdjson::SUCCESS )
					co_return http::Response::error( 400, "'tag_ids' must be an array of integers" );

				std::vector< TagID > tag_ids {};
				for ( simdjson::dom::element id_element : tag_ids_array )
				{
					std::int64_t id {};
					if ( id_element.get_int64().get( id ) != simdjson::SUCCESS )
						co_return http::Response::error( 400, "'tag_ids' must be an array of integers" );
					tag_ids.emplace_back( static_cast< TagID >( id ) );
				}
				query.addPositiveTags( tag_ids );
			}
		}

		std::vector< TagDomainID > tag_domains {};
		simdjson::dom::array tag_domains_array {};
		const auto tag_domains_ec { root[ "tag_domains" ].get( tag_domains_array ) };
		if ( tag_domains_ec != simdjson::NO_SUCH_FIELD )
		{
			if ( tag_domains_ec != simdjson::SUCCESS )
				co_return http::Response::error( 400, "'tag_domains' must be an array of integers" );

			for ( simdjson::dom::element id_element : tag_domains_array )
			{
				std::int64_t id {};
				if ( id_element.get_int64().get( id ) != simdjson::SUCCESS )
					co_return http::Response::error( 400, "'tag_domains' must be an array of integers" );
				tag_domains.emplace_back( static_cast< TagDomainID >( id ) );
			}
		}

		std::string_view display_view {};
		if ( root[ "display" ].get( display_view ) == simdjson::SUCCESS && display_view == "storage" )
			query.setDisplay( HydrusDisplayType::STORED );
		else
			query.setDisplay( HydrusDisplayType::DISPLAY );

		SortType sort_type { SortType::IMPORT_TIME };
		SortOrder sort_order { SortOrder::DESC };
		simdjson::dom::object sort_object {};
		const auto sort_ec { root[ "sort" ].get( sort_object ) };
		if ( sort_ec != simdjson::NO_SUCH_FIELD )
		{
			if ( sort_ec != simdjson::SUCCESS ) co_return http::Response::error( 400, "'sort' must be an object" );

			std::string_view by {};
			if ( sort_object[ "by" ].get( by ) == simdjson::SUCCESS ) sort_type = parseSortType( std::string { by } );

			std::string_view order {};
			if ( sort_object[ "order" ].get( order ) == simdjson::SUCCESS )
				sort_order = order == "asc" ? SortOrder::ASC : SortOrder::DESC;
		}
		query.setSortType( sort_type );
		query.setSortOrder( sort_order );

		std::optional< std::int64_t > limit_param {};
		std::int64_t limit_value {};
		if ( root[ "limit" ].get( limit_value ) == simdjson::SUCCESS )
		{
			if ( limit_value < 0 ) co_return http::Response::error( 400, "'limit' must be a non-negative integer" );
			limit_param = limit_value;
			query.setLimit( static_cast< std::size_t >( limit_value ) );
		}

		std::int64_t offset_value {};
		if ( root[ "offset" ].get( offset_value ) == simdjson::SUCCESS )
		{
			if ( offset_value < 0 ) co_return http::Response::error( 400, "'offset' must be a non-negative integer" );
			query.setOffset( static_cast< std::size_t >( offset_value ) );
		}

		bool return_ids { true };
		bool return_hashes { false };
		simdjson::dom::array return_array {};
		if ( root[ "return" ].get( return_array ) == simdjson::SUCCESS )
		{
			return_ids = false;
			for ( simdjson::dom::element entry : return_array )
			{
				std::string_view value {};
				if ( entry.get_string().get( value ) != simdjson::SUCCESS ) continue;
				if ( value == "ids" )
					return_ids = true;
				else if ( value == "hashes" )
					return_hashes = true;
			}
			if ( !return_ids && !return_hashes ) return_ids = true;
		}

		const auto result { co_await query.query( db, std::move( tag_domains ), return_ids, return_hashes ) };

		simdjson::builder::string_builder builder;
		builder.start_object();
		bool first_field { true };

		const auto nextField = [ &builder, &first_field ]
		{
			if ( !first_field ) builder.append_comma();
			first_field = false;
		};

		if ( return_ids )
		{
			nextField();
			builder.escape_and_append_with_quotes( "record_ids" );
			builder.append_colon();
			builder.start_array();
			bool first_id { true };
			for ( const auto id : result.record_ids )
			{
				if ( !first_id ) builder.append_comma();
				builder.append( id );
				first_id = false;
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
				builder.append( hash.hex() );
				first_hash = false;
			}
			builder.end_array();
		}

		const auto count { static_cast< std::int64_t >( result.size() ) };
		nextField();
		builder.append_key_value( "count", count );

		nextField();
		builder.append_key_value( "truncated", limit_param.has_value() && count == *limit_param );

		const auto elapsed {
			std::chrono::duration_cast< std::chrono::milliseconds >( std::chrono::steady_clock::now() - start ).count()
		};
		nextField();
		builder.append_key_value( "query_ms", static_cast< std::int64_t >( elapsed ) );

		bool debug_requested { false };
		if ( bool debug_value {}; root[ "debug" ].get( debug_value ) == simdjson::SUCCESS )
			debug_requested = debug_value;

		if ( debug_requested && query.stats() )
		{
			nextField();
			builder.escape_and_append_with_quotes( "stats" );
			builder.append_colon();
			builder.start_array();
			bool first_step { true };
			for ( const auto& step : query.stats()->steps() )
			{
				if ( !first_step ) builder.append_comma();
				first_step = false;

				builder.start_object();
				builder.append_key_value( "step", step.label );
				builder.append_comma();
				builder.append_key_value( "rows", static_cast< std::int64_t >( step.rows ) );
				builder.append_comma();
				builder.append_key_value( "inverted", step.inverted );
				builder.append_comma();
				builder.escape_and_append_with_quotes( "kind" );
				builder.append_colon();
				switch ( step.kind )
				{
					case search::StepKind::Fetch:
						builder.escape_and_append_with_quotes( "fetch" );
						break;
					case search::StepKind::Fold:
						builder.escape_and_append_with_quotes( "fold" );
						break;
					case search::StepKind::Page:
						builder.escape_and_append_with_quotes( "page" );
						break;
				}
				if ( step.micros > 0 )
				{
					builder.append_comma();
					builder.append_key_value( "micros", step.micros );
				}
				builder.end_object();
			}
			builder.end_array();
		}

		builder.end_object();

		auto json { builder.view() };
		if ( json.error() ) throw std::runtime_error( simdjson::error_message( json.error() ) );

		co_return http::Response::json( 200, folly::IOBuf::copyBuffer( *json ) );
	}
	catch ( const std::invalid_argument& e )
	{
		co_return http::Response::error( 400, e.what() );
	}
	catch ( const simdjson::simdjson_error& e )
	{
		co_return http::Response::error( 400, e.what() );
	}
}

http::RouteDefinition searchRoute( db::DBContext& db )
{
	auto handler = [ &db ]( http::RequestContext context ) -> http::HTTPTask< http::Response >
	{ co_return co_await search( db, std::move( context ) ); };

	return http::RouteDefinition {
		http::RouteMetadata { proxygen::HTTPMethod::GET, "/search", {}, { apiAuthValidation( db ) } },
		std::move( handler )
	};
}

http::RouteDefinition searchPostRoute( db::DBContext& db )
{
	auto handler = [ &db ]( http::RequestContext context ) -> http::HTTPTask< http::Response >
	{ co_return co_await searchPost( db, std::move( context ) ); };

	return http::RouteDefinition {
		http::RouteMetadata { proxygen::HTTPMethod::POST, "/search", {}, { apiAuthValidation( db ) } },
		std::move( handler )
	};
}

} // namespace idhan::httpapi
