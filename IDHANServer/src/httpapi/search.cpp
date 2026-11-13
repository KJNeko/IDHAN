#include "search.hpp"

#include <folly/io/IOBuf.h>

#include <chrono>
#include <format>
#include <simdjson.h>
#include <string>
#include <vector>

#include "apiAuth.hpp"
#include "idValidation.hpp"
#include "idhan/db/DBContext.hpp"
#include "idhan/http/routing/ReflectedRoutes.hpp"
#include "jsonBody.hpp"
#include "logging/log.hpp"
#include "search/SearchQuery.hpp"
#include "search/parseSortType.hpp"

namespace idhan::httpapi
{
[[nodiscard]] http::HTTPTask< http::JsonResult< std::vector< RecordID > > > searchRecords(
	db::DBContext& db,
	http::Query< SearchParameters > parameters )
{
	auto tag_ids { std::move( parameters.value.tag_ids ) };
	if ( tag_ids.empty() ) tag_ids = std::move( parameters.value.tag_id );

	SearchQuery query {};
	query.setIncludeDeleted( parameters.value.include_deleted.value_or( false ) );
	query.addPositiveTags( tag_ids );

	if ( parameters.value.by )
	{
		const auto sort_type { parseSortType( *parameters.value.by ) };
		if ( !sort_type )
			co_return http::Response::error( 400, std::format( "Unknown sorting method '{}'", *parameters.value.by ) );

		query.setSortType( *sort_type );
	}
	if ( parameters.value.order )
		query.setSortOrder( *parameters.value.order == "desc" ? SortOrder::DESC : SortOrder::ASC );

	const auto result { co_await query.query( db, std::move( parameters.value.tag_domains ) ) };

	co_return http::JsonResponse< std::vector< RecordID > > { result.record_ids };
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

[[nodiscard]] http::HTTPTask< http::JsonResult< SearchRecordsResponse > > searchPost(
	db::DBContext& db,
	http::RequestContext context )
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
		bool include_deleted {};
		const auto include_deleted_error { root[ "include_deleted" ].get( include_deleted ) };
		if ( include_deleted_error != simdjson::SUCCESS && include_deleted_error != simdjson::NO_SUCH_FIELD )
			co_return http::Response::error( 400, "'include_deleted' must be a boolean" );

		query.setIncludeDeleted( include_deleted );

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
					auto id { parsePositiveId< TagID >( id_element, "tag_id" ) };
					if ( !id ) co_return std::move( id.error() );

					tag_ids.emplace_back( *id );
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
				auto id { parsePositiveId< TagDomainID >( id_element, "tag_domain_id" ) };
				if ( !id ) co_return std::move( id.error() );

				tag_domains.emplace_back( *id );
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
			const auto by_ec { sort_object[ "by" ].get( by ) };
			if ( by_ec != simdjson::NO_SUCH_FIELD )
			{
				if ( by_ec != simdjson::SUCCESS ) co_return http::Response::error( 400, "'sort.by' must be a string" );

				const auto parsed_sort { parseSortType( by ) };
				if ( !parsed_sort )
					co_return http::Response::error( 400, std::format( "Unknown sorting method '{}'", by ) );

				sort_type = *parsed_sort;
			}

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

		SearchRecordsResponse response {};
		if ( return_ids ) response.record_ids.emplace( result.record_ids );

		if ( return_hashes )
		{
			auto& hashes { response.hashes.emplace() };
			hashes.reserve( result.hashes.size() );
			for ( const auto& hash : result.hashes ) hashes.push_back( hash.hex() );
		}

		response.count = static_cast< std::int64_t >( result.size() );
		response.truncated = limit_param.has_value() && response.count == *limit_param;
		response.query_ms =
			std::chrono::duration_cast< std::chrono::milliseconds >( std::chrono::steady_clock::now() - start ).count();

		bool debug_requested {};
		bool debug_value {};
		if ( root[ "debug" ].get( debug_value ) == simdjson::SUCCESS ) debug_requested = debug_value;

		if ( debug_requested && query.stats() )
		{
			auto& stats { response.stats.emplace() };
			for ( const auto& step : query.stats()->steps() )
			{
				SearchRecordsResponse::Step output {
					step.label, static_cast< std::int64_t >( step.rows ), step.inverted, {}, {}
				};
				switch ( step.kind )
				{
					case search::StepKind::Fetch:
						output.kind = "fetch";
						break;
					case search::StepKind::Fold:
						output.kind = "fold";
						break;
					case search::StepKind::Page:
						output.kind = "page";
						break;
				}
				if ( step.micros > 0 ) output.micros.emplace( step.micros );

				stats.push_back( std::move( output ) );
			}
		}

		co_return http::JsonResponse< SearchRecordsResponse > { std::move( response ) };
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

} // namespace idhan::httpapi
