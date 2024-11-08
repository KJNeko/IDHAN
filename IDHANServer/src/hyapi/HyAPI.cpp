//
// Created by kj16609 on 11/6/24.
//

#include "HyAPI.hpp"

#include "IDHANTypes.hpp"
#include "constants/SearchOrder.hpp"
#include "constants/hydrus_version.hpp"
#include "core/SearchBuilder.hpp"
#include "fixme.hpp"
#include "logging/log.hpp"
#include "versions.hpp"

namespace idhan::hyapi
{

	void HydrusAPI::unsupported( const drogon::HttpRequestPtr& request, ResponseFunction&& callback )
	{
		Json::Value root;
		root[ "status" ] = 410;
		root[ "message" ] = "IDHAN Hydrus API does not support this request";
	}

	// /hyapi/api_version
	void HydrusAPI::apiVersion( [[maybe_unused]] const drogon::HttpRequestPtr& request, ResponseFunction&& callback )
	{
		Json::Value json;
		json[ "version" ] = HYDRUS_MIMICED_API_VERSION;
		json[ "hydrus_version" ] = HYDRUS_MIMICED_VERSION;

		// I'm unsure if anything would actually ever need this.
		// But i figured i'd supply it anyways
		json[ "idhan_server_version" ] = IDHAN_VERSION;
		json[ "idhan_api_version" ] = IDHAN_API_VERSION;

		const auto response { drogon::HttpResponse::newHttpJsonResponse( json ) };

		callback( response );
	}

	// /hyapi/access/request_new_permissions
	void HydrusAPI::requestNewPermissions( const drogon::HttpRequestPtr& request, ResponseFunction&& callback )
	{
		idhan::fixme();
	}

	// /hyapi/access/session_key
	void HydrusAPI::sessionKey( const drogon::HttpRequestPtr& request, ResponseFunction&& callback )
	{
		idhan::fixme();
	}

	// /hyapi/access/verify_access_key
	void HydrusAPI::verifyAccessKey( const drogon::HttpRequestPtr& request, ResponseFunction&& callback )
	{
		Json::Value json;
		json[ "basic_permissions" ] = 0;
		json[ "human_description" ] = "";

		const auto response { drogon::HttpResponse::newHttpJsonResponse( json ) };

		callback( response );
	}

	void HydrusAPI::getService( const drogon::HttpRequestPtr& request, ResponseFunction&& callback )
	{}

	void HydrusAPI::getServices( const drogon::HttpRequestPtr& request, ResponseFunction&& callback )
	{}

	void HydrusAPI::addFile( const drogon::HttpRequestPtr& request, ResponseFunction&& callback )
	{}

	template < typename T >
	T getDefaultedValue( const std::string name, const drogon::HttpRequestPtr& request, const T default_value )
	{
		const auto value { request->getOptionalParameter< T >( name ) };

		if ( value.has_value() ) return value.value();

		return default_value;
	}

	void HydrusAPI::searchFiles( const drogon::HttpRequestPtr& request, ResponseFunction&& callback )
	{
		const auto tags { request->getOptionalParameter< std::string >( "tags" ) };
		if ( !tags.has_value() )
		{
			Json::Value value;
			value[ "status" ] = 403;
			value[ "message" ] = "You must supply a list of tags.";

			callback( drogon::HttpResponse::newHttpJsonResponse( value ) );

			return;
		}

		// was a domain set?
		const auto file_domain_id { request->getOptionalParameter< int >( "file_domain" ) };
		const auto tag_domain_id { request->getOptionalParameter< std::size_t >( "tag_service_key" ) };
		const auto include_current_tags { getDefaultedValue< bool >( "include_current_tags", request, true ) };
		const auto include_pending_tags { getDefaultedValue< bool >( "include_pending_tags", request, true ) };
		const auto file_sort_type {
			getDefaultedValue< SearchOrder >( "file_sort_type", request, SearchOrder::ImportTime )
		};
		const auto file_sort_asc { getDefaultedValue< bool >( "file_sort_asc", request, true ) };
		const auto return_file_ids { getDefaultedValue( "return_file_ids", request, true ) };
		const auto return_hashes { getDefaultedValue< bool >( "return_hashes", request, false ) };

		// Build the search
		SearchBuilder builder {};

		if ( file_domain_id.has_value() ) builder.filterFileDomain( file_domain_id.value() );

		if ( tag_domain_id.has_value() ) builder.filterTagDomain( tag_domain_id.value() );

		std::string query { builder.construct() };
	}

	void HydrusAPI::fileHashes( const drogon::HttpRequestPtr& request, ResponseFunction&& callback )
	{}

	void HydrusAPI::fileMetadata( const drogon::HttpRequestPtr& request, ResponseFunction&& callback )
	{}

	void HydrusAPI::file( const drogon::HttpRequestPtr& request, ResponseFunction&& callback )
	{}

	void HydrusAPI::thumbnail( const drogon::HttpRequestPtr& request, ResponseFunction&& callback )
	{}

} // namespace idhan::hyapi
