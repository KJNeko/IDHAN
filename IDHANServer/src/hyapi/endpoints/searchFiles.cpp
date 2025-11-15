//
// Created by kj16609 on 11/14/25.
//

#include "api/helpers/createBadRequest.hpp"
#include "core/search/SearchBuilder.hpp"
#include "crypto/SHA256.hpp"
#include "hyapi/HyAPI.hpp"
#include "logging/ScopedTimer.hpp"

namespace idhan::hyapi
{

drogon::Task< drogon::HttpResponsePtr > HydrusAPI::searchFiles( drogon::HttpRequestPtr request )
{
	logging::ScopedTimer timer { "Search files", std::chrono::seconds( 2 ) };
	const auto start = std::chrono::system_clock::now();
	const auto tags_o { request->getOptionalParameter< std::string >( "tags" ) };
	constexpr auto empty_tags { "[]" };
	const auto tags_parameter_str { tags_o.value_or( empty_tags ) };
	if ( tags_parameter_str == empty_tags )
	{
		Json::Value value;
		value[ "file_ids" ] = Json::Value( Json::arrayValue );

		co_return drogon::HttpResponse::newHttpJsonResponse( value );
	}

	auto db { drogon::app().getDbClient() };

	// Build the search
	SearchBuilder builder {};

	Json::Value tags_json {};
	Json::Reader reader;
	if ( !reader.parse( tags_parameter_str, tags_json ) )
	{
		// Try to decode the text again and re-parse it
		const auto decoded_tags { drogon::utils::urlDecode( tags_parameter_str ) };
		if ( reader.parse( decoded_tags, tags_json ) )
		{
			log::warn( "Tags JSON had to be URL-decoded a second time. Call the requester an idiot" );
		}
		else
		{
			co_return createBadRequest( "Invalid tags json: Was {}", tags_parameter_str );
		}
	}

	std::vector< std::string > search_tags {};
	search_tags.reserve( tags_json.size() );
	std::vector< std::string > system_tags {};

	for ( const auto& tag : tags_json )
	{
		const auto tag_text { tag.asString() };
		if ( tag_text.starts_with( "system:" ) )
		{
			system_tags.emplace_back( tag_text );
			continue;
		}

		search_tags.emplace_back( tag_text );
	}

	const auto search_result { co_await builder.setTags( search_tags ) };
	builder.setSystemTags( system_tags );

	// TODO: file domains. For now we'll assume all files

	// TODO: Tag service key, Which tag domain to search. Defaults to all tags

	// include_current_tags and include_pending_tags are both things that are not needed for IDHAN so we just skip this.

	const auto file_sort_type { static_cast< HydrusSortType >(
		request->getOptionalParameter< std::uint64_t >( "file_sort_type" ).value_or( HydrusSortType::DEFAULT ) ) };

	const auto file_sort_asc { request->getOptionalParameter< bool >( "file_sort_asc" ).value_or( true ) };

	builder.setSortType( hyToIDHANSortType( file_sort_type ) );
	builder.setSortOrder( file_sort_asc ? SortOrder::ASC : SortOrder::DESC );

	const auto return_file_ids { request->getOptionalParameter< bool >( "return_file_ids" ).value_or( true ) };
	const auto return_hashes { request->getOptionalParameter< bool >( "return_hashes" ).value_or( false ) };
	const auto tag_display_type {
		request->getOptionalParameter< std::string >( "tag_display_type" ).value_or( "display" )
	};

	if ( tag_display_type == std::string_view( "storage" ) )
	{
		builder.setDisplay( HydrusDisplayType::STORED );
	}
	else
	{
		builder.setDisplay( HydrusDisplayType::DISPLAY );
	}

	auto end = std::chrono::system_clock::now();
	const auto diff { std::chrono::duration_cast< std::chrono::milliseconds >( end - start ).count() };
	log::info( "Setup took {}ms", diff );

	auto query_start = std::chrono::system_clock::now();
	const auto result { co_await builder.query( db, {} ) };
	auto query_end = std::chrono::system_clock::now();
	const auto query_diff {
		std::chrono::duration_cast< std::chrono::milliseconds >( query_end - query_start ).count()
	};
	log::info( "Query took {}ms", query_diff );

	Json::Value out {};

	const auto json_start = std::chrono::system_clock::now();
	Json::Value file_ids {};
	Json::Value hashes {};
	Json::ArrayIndex i { 0 };

	file_ids.resize( static_cast< Json::Value::ArrayIndex >( result.size() ) );
	hashes.resize( static_cast< Json::Value::ArrayIndex >( result.size() ) );

	for ( const auto& row : result )
	{
		if ( return_file_ids ) file_ids[ i ] = row[ "record_id" ].as< RecordID >();
		if ( return_hashes ) hashes[ i ] = SHA256::fromPgCol( row[ "sha256" ] ).hex();

		i += 1;
	}

	if ( return_file_ids ) out[ "file_ids" ] = std::move( file_ids );
	if ( return_hashes ) out[ "hashes" ] = std::move( hashes );

	const auto json_end = std::chrono::system_clock::now();
	const auto json_diff { std::chrono::duration_cast< std::chrono::milliseconds >( json_end - json_start ).count() };
	log::info( "JSON took {}ms", json_diff );

	co_return drogon::HttpResponse::newHttpJsonResponse( out );
}

} // namespace idhan::hyapi