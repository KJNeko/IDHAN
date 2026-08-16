
#include <chrono>

#include "api/SearchAPI.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "api/search/parseSortType.hpp"
#include "core/search/SearchBuilder.hpp"
#include "crypto/SHA256.hpp"

namespace idhan::api
{

drogon::Task< drogon::HttpResponsePtr > SearchAPI::searchPost( drogon::HttpRequestPtr request )
{
	const auto start { std::chrono::steady_clock::now() };

	const auto body { request->getJsonObject() };
	if ( !body || !body->isObject() ) co_return createBadRequest( "Request body must be a JSON object" );
	const Json::Value& json { *body };

	SearchBuilder builder {};

	if ( json.isMember( "tags" ) )
	{
		if ( !json[ "tags" ].isArray() ) co_return createBadRequest( "'tags' must be an array of strings" );

		std::vector< std::string > text_tags {};
		std::vector< std::string > system_tags {};
		std::vector< std::string > wildcard_namespaces {};
		std::vector< std::string > wildcard_tags {};
		for ( const auto& tag : json[ "tags" ] )
		{
			if ( !tag.isString() ) co_return createBadRequest( "'tags' must be an array of strings" );
			auto tag_text { tag.asString() };
			const bool is_namespace_wildcard {
				tag_text.ends_with( ":*" ) && tag_text.find( '*' ) == tag_text.size() - 1
			};

			if ( tag_text.starts_with( "system:" ) )
				system_tags.emplace_back( std::move( tag_text ) );
			else if ( is_namespace_wildcard )
				wildcard_namespaces.emplace_back( std::move( tag_text ) );
			else if ( tag_text.contains( "*" ) )
				wildcard_tags.emplace_back( std::move( tag_text ) );
			else
				text_tags.emplace_back( std::move( tag_text ) );
		}

		log::info(
			"Search got {} tags, {} system tags, {} wildcard namespaces, {} wildcard tags",
			text_tags.size(),
			system_tags.size(),
			wildcard_namespaces.size(),
			wildcard_tags.size() );

		const auto tag_result_error { co_await builder.setTags( text_tags ) };
		if ( tag_result_error ) co_return *tag_result_error;
		const auto namespace_result_error { co_await builder.setWildcardNamespaces( wildcard_namespaces ) };
		if ( namespace_result_error ) co_return *namespace_result_error;
		const auto wildcard_result_error { co_await builder.setWildcardTags( wildcard_tags ) };
		if ( wildcard_result_error ) co_return *wildcard_result_error;

		try
		{
			builder.setSystemTags( system_tags );
		}
		catch ( const std::invalid_argument& e )
		{
			co_return createBadRequest( "Invalid system tag: {}", e.what() );
		}
	}
	else if ( json.isMember( "tag_ids" ) )
	{
		// Alternative to text tags: positive tag ids directly.
		if ( !json[ "tag_ids" ].isArray() ) co_return createBadRequest( "'tag_ids' must be an array of integers" );

		std::vector< TagID > tag_ids {};
		for ( const auto& id : json[ "tag_ids" ] )
		{
			if ( !id.isIntegral() ) co_return createBadRequest( "'tag_ids' must be an array of integers" );
			tag_ids.emplace_back( static_cast< TagID >( id.asInt64() ) );
		}
		builder.addPositiveTags( tag_ids );
	}

	std::vector< TagDomainID > tag_domains {};
	if ( json.isMember( "tag_domains" ) )
	{
		if ( !json[ "tag_domains" ].isArray() )
			co_return createBadRequest( "'tag_domains' must be an array of integers" );
		for ( const auto& id : json[ "tag_domains" ] )
		{
			if ( !id.isIntegral() ) co_return createBadRequest( "'tag_domains' must be an array of integers" );
			tag_domains.emplace_back( static_cast< TagDomainID >( id.asInt64() ) );
		}
	}

	if ( json.isMember( "display" ) && json[ "display" ].isString() && json[ "display" ].asString() == "storage" )
		builder.setDisplay( HydrusDisplayType::STORED );
	else
		builder.setDisplay( HydrusDisplayType::DISPLAY );

	SortType sort_type { SortType::IMPORT_TIME };
	SortOrder sort_order { SortOrder::DESC };
	if ( json.isMember( "sort" ) )
	{
		const auto& sort { json[ "sort" ] };
		if ( !sort.isObject() ) co_return createBadRequest( "'sort' must be an object" );
		if ( sort.isMember( "by" ) && sort[ "by" ].isString() ) sort_type = parseSortType( sort[ "by" ].asString() );
		if ( sort.isMember( "order" ) && sort[ "order" ].isString() )
			sort_order = sort[ "order" ].asString() == "asc" ? SortOrder::ASC : SortOrder::DESC;
	}
	builder.setSortType( sort_type );
	builder.setSortOrder( sort_order );

	if ( json.isMember( "limit" ) )
	{
		if ( !json[ "limit" ].isIntegral() || json[ "limit" ].asInt64() < 0 )
			co_return createBadRequest( "'limit' must be a non-negative integer" );
		builder.setLimit( static_cast< std::size_t >( json[ "limit" ].asInt64() ) );
	}
	if ( json.isMember( "offset" ) )
	{
		if ( !json[ "offset" ].isIntegral() || json[ "offset" ].asInt64() < 0 )
			co_return createBadRequest( "'offset' must be a non-negative integer" );
		builder.setOffset( static_cast< std::size_t >( json[ "offset" ].asInt64() ) );
	}

	bool return_ids { true };
	bool return_hashes { false };
	if ( json.isMember( "return" ) && json[ "return" ].isArray() )
	{
		return_ids = false;
		for ( const auto& r : json[ "return" ] )
		{
			if ( !r.isString() ) continue;
			const auto s { r.asString() };
			if ( s == "ids" )
				return_ids = true;
			else if ( s == "hashes" )
				return_hashes = true;
		}
		if ( !return_ids && !return_hashes ) return_ids = true;
	}

	auto db { drogon::app().getDbClient() };
	const auto result { co_await builder.query( db, std::move( tag_domains ), return_ids, return_hashes ) };

	Json::Value out {};

	if ( return_ids )
	{
		Json::Value ids { Json::arrayValue };
		for ( const auto id : result.record_ids ) ids.append( id );
		out[ "record_ids" ] = std::move( ids );
	}
	if ( return_hashes )
	{
		Json::Value hashes { Json::arrayValue };
		for ( const auto& hash : result.hashes ) hashes.append( hash.hex() );
		out[ "hashes" ] = std::move( hashes );
	}

	const auto count { static_cast< std::int64_t >( result.size() ) };
	out[ "count" ] = count;

	const auto limit_param {
		json.isMember( "limit" ) && json[ "limit" ].isIntegral() ?
			std::optional< std::int64_t > { json[ "limit" ].asInt64() } :
			std::nullopt
	};
	out[ "truncated" ] = limit_param.has_value() && count == *limit_param;

	const auto elapsed {
		std::chrono::duration_cast< std::chrono::milliseconds >( std::chrono::steady_clock::now() - start ).count()
	};
	out[ "query_ms" ] = static_cast< std::int64_t >( elapsed );

	if ( json.isMember( "debug" ) && json[ "debug" ].isBool() && json[ "debug" ].asBool() && builder.stats() )
	{
		Json::Value steps { Json::arrayValue };
		for ( const auto& step : builder.stats()->steps() )
		{
			Json::Value entry {};
			entry[ "step" ] = step.label;
			entry[ "rows" ] = static_cast< Json::Int64 >( step.rows );
			entry[ "inverted" ] = step.inverted;
			switch ( step.kind )
			{
				case search::StepKind::Fetch:
					entry[ "kind" ] = "fetch";
					break;
				case search::StepKind::Fold:
					entry[ "kind" ] = "fold";
					break;
				case search::StepKind::Page:
					entry[ "kind" ] = "page";
					break;
			}
			if ( step.micros > 0 ) entry[ "micros" ] = static_cast< Json::Int64 >( step.micros );
			steps.append( std::move( entry ) );
		}
		out[ "stats" ] = std::move( steps );
	}

	co_return drogon::HttpResponse::newHttpJsonResponse( out );
}

} // namespace idhan::api
