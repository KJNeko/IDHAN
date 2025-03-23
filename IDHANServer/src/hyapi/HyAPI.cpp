//
// Created by kj16609 on 11/6/24.
//

#include "HyAPI.hpp"

#include "IDHANTypes.hpp"
#include "api/IDHANSearchAPI.hpp"
#include "constants/SearchOrder.hpp"
#include "constants/hydrus_version.hpp"
#include "core/SearchBuilder.hpp"
#include "fixme.hpp"
#include "logging/log.hpp"
#include "versions.hpp"

namespace idhan::hyapi
{

drogon::Task< drogon::HttpResponsePtr > HydrusAPI::unsupported( const drogon::HttpRequestPtr& request )
{
	Json::Value root;
	root[ "status" ] = 410;
	root[ "message" ] = "IDHAN Hydrus API does not support this request";
}

// /hyapi/api_version
drogon::Task< drogon::HttpResponsePtr > HydrusAPI::apiVersion( [[maybe_unused]] const drogon::HttpRequestPtr& request )
{
	Json::Value json;
	json[ "version" ] = HYDRUS_MIMICED_API_VERSION;
	json[ "hydrus_version" ] = HYDRUS_MIMICED_VERSION;

	// I'm unsure if anything would actually ever need this.
	// But i figured i'd supply it anyways
	json[ "is_idhan_instance" ] = true;
	json[ "idhan_api_version" ] = IDHAN_API_VERSION;

	const auto response { drogon::HttpResponse::newHttpJsonResponse( json ) };

	co_return response;
}

// /hyapi/access/request_new_permissions
drogon::Task< drogon::HttpResponsePtr > HydrusAPI::requestNewPermissions( const drogon::HttpRequestPtr& request )
{
	idhan::fixme();
}

// /hyapi/access/session_key
drogon::Task< drogon::HttpResponsePtr > HydrusAPI::sessionKey( const drogon::HttpRequestPtr& request )
{
	idhan::fixme();
}

// /hyapi/access/verify_access_key
drogon::Task< drogon::HttpResponsePtr > HydrusAPI::verifyAccessKey( const drogon::HttpRequestPtr& request )
{
	Json::Value json;
	json[ "basic_permissions" ] = 0;
	json[ "human_description" ] = "";

	const auto response { drogon::HttpResponse::newHttpJsonResponse( json ) };

	co_return response;
}

drogon::Task< drogon::HttpResponsePtr > HydrusAPI::getService( const drogon::HttpRequestPtr& request )
{}

drogon::Task< drogon::HttpResponsePtr > HydrusAPI::getServices( const drogon::HttpRequestPtr& request )
{}

drogon::Task< drogon::HttpResponsePtr > HydrusAPI::addFile( const drogon::HttpRequestPtr& request )
{}

template < typename T >
T getDefaultedValue( const std::string name, const drogon::HttpRequestPtr& request, const T default_value )
{
	return request->getOptionalParameter< T >( name ).value_or( default_value );
}

drogon::Task< drogon::HttpResponsePtr > HydrusAPI::searchFiles( drogon::HttpRequestPtr request )
{
	const auto tags { request->getOptionalParameter< std::string >( "tags" ) };
	if ( !tags.has_value() )
	{
		Json::Value value;
		value[ "status" ] = 403;
		value[ "message" ] = "You must supply a list of tags.";

		co_return drogon::HttpResponse::newHttpJsonResponse( value );
	}

	log::debug( "tags: {}", tags.value() );

	// modify the request and resubmit it under the /search endpoint
	co_return co_await ::idhan::api::IDHANSearchAPI::search( request );

	// Build the search
	SearchBuilder builder {};

	if ( auto opt = request->getOptionalParameter< int >( "file_domain_id" ); opt.has_value() )
	{
		builder.addFileDomain( opt.value() );
	}

	if ( auto opt = request->getOptionalParameter< std::vector< int > >( "file_domain_ids" ); opt.has_value() )
	{
		for ( const auto& value : opt.value() ) builder.addFileDomain( value );
	}

	// was a domain set?
	const auto file_domain_id { request->getOptionalParameter< int >( "file_domain" ) };

	const auto tag_domain_id { request->getOptionalParameter< TagDomainID >( "tag_service_key" )
		                           .value_or( std::numeric_limits< TagDomainID >::max() ) };

	const auto include_current_tags {
		request->getOptionalParameter< bool >( "include_current_tags" ).value_or( true )
	};

	const auto include_pending_tags {
		request->getOptionalParameter< bool >( "include_pending_tags" ).value_or( true )
	};

	const auto file_sort_type {
		request->getOptionalParameter< SearchOrder >( "file_sort_type" ).value_or( SearchOrder::ImportTime )
	};

	const auto file_sort_asc { request->getOptionalParameter< bool >( "file_sort_asc" ).value_or( true ) };
	const auto return_file_ids { request->getOptionalParameter< bool >( "return_file_ids" ).value_or( true ) };
	const auto return_hashes { request->getOptionalParameter< bool >( "return_hashes" ).value_or( false ) };

	std::string query { builder.construct() };
}

drogon::Task< drogon::HttpResponsePtr > HydrusAPI::fileHashes( const drogon::HttpRequestPtr& request )
{}

drogon::Task< drogon::HttpResponsePtr > HydrusAPI::fileMetadata( const drogon::HttpRequestPtr& request )
{}

drogon::Task< drogon::HttpResponsePtr > HydrusAPI::file( const drogon::HttpRequestPtr& request )
{}

drogon::Task< drogon::HttpResponsePtr > HydrusAPI::thumbnail( const drogon::HttpRequestPtr& request )
{}

} // namespace idhan::hyapi
