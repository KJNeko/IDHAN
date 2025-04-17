//
// Created by kj16609 on 11/6/24.
//

#include "HyAPI.hpp"

#include "IDHANTypes.hpp"
#include "api/IDHANSearchAPI.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "constants/SearchOrder.hpp"
#include "constants/hydrus_version.hpp"
#include "core/SearchBuilder.hpp"
#include "crypto/SHA256.hpp"
#include "fixme.hpp"
#include "logging/log.hpp"
#include "versions.hpp"

namespace idhan::hyapi
{

drogon::Task< drogon::HttpResponsePtr > HydrusAPI::unsupported( drogon::HttpRequestPtr request )
{
	Json::Value root;
	root[ "status" ] = 410;
	root[ "message" ] = "IDHAN Hydrus API does not support this request";
}

// /hyapi/api_version
drogon::Task< drogon::HttpResponsePtr > HydrusAPI::apiVersion( [[maybe_unused]] drogon::HttpRequestPtr request )
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
drogon::Task< drogon::HttpResponsePtr > HydrusAPI::requestNewPermissions( drogon::HttpRequestPtr request )
{
	idhan::fixme();
}

// /hyapi/access/session_key
drogon::Task< drogon::HttpResponsePtr > HydrusAPI::sessionKey( drogon::HttpRequestPtr request )
{
	idhan::fixme();
}

// /hyapi/access/verify_access_key
drogon::Task< drogon::HttpResponsePtr > HydrusAPI::verifyAccessKey( drogon::HttpRequestPtr request )
{
	Json::Value json;
	json[ "basic_permissions" ] = 0;
	json[ "human_description" ] = "";

	const auto response { drogon::HttpResponse::newHttpJsonResponse( json ) };

	co_return response;
}

drogon::Task< drogon::HttpResponsePtr > HydrusAPI::getService( drogon::HttpRequestPtr request )
{}

drogon::Task< drogon::HttpResponsePtr > HydrusAPI::getServices( drogon::HttpRequestPtr request )
{}

drogon::Task< drogon::HttpResponsePtr > HydrusAPI::addFile( drogon::HttpRequestPtr request )
{}

template < typename T >
T getDefaultedValue( const std::string name, drogon::HttpRequestPtr request, const T default_value )
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

drogon::Task< drogon::HttpResponsePtr > HydrusAPI::fileHashes( drogon::HttpRequestPtr request )
{}

drogon::Task< drogon::HttpResponsePtr > HydrusAPI::fileMetadata( drogon::HttpRequestPtr request )
{
	const auto file_ids { request->getOptionalParameter< std::string >( "file_ids" ) };
	if ( !file_ids.has_value() ) co_return createBadRequest( "Must provide file_ids array" );

	std::string file_ids_str { file_ids.value() };
	//file_ids will be in a json string format
	std::vector< RecordID > record_ids {};
	file_ids_str = file_ids_str.substr( 1, file_ids_str.size() - 2 ); // cut off the []
	while ( !file_ids_str.empty() )
	{
		const auto end_itter { file_ids_str.find_first_of( ',' ) };
		if ( end_itter == std::string::npos )
		{
			record_ids.push_back( std::stoi( file_ids_str ) );
			file_ids_str.clear();
		}
		else
		{
			record_ids.push_back( std::stoi( file_ids_str.substr( 0, end_itter ) ) );
			file_ids_str = file_ids_str.substr( end_itter + 1 );
		}
	}

	// we've gotten all the ids. For now we'll just return them
	Json::Value metadata {};

	auto db { drogon::app().getDbClient() };

	for ( const auto& id : record_ids )
	{
		Json::Value data {};

		const auto hash_result { co_await db->execSqlCoro( "SELECT sha256 FROM records WHERE record_id = $1", id ) };

		data[ "file_id" ] = id;
		const SHA256 sha256 { hash_result[ 0 ][ "sha256" ] };
		data[ "hash" ] = sha256.hex();

		metadata.append( std::move( data ) );
	}

	Json::Value out {};
	out[ "metadata" ] = std::move( metadata );

	co_return drogon::HttpResponse::newHttpJsonResponse( std::move( out ) );
}

/*
drogon::Task< drogon::HttpResponsePtr > HydrusAPI::file( drogon::HttpRequestPtr request )
{
	co_return drogon::HttpResponse::newHttpResponse();
}
*/

drogon::Task< drogon::HttpResponsePtr > HydrusAPI::file( drogon::HttpRequestPtr request )
{
	auto file_id { request->getOptionalParameter< RecordID >( "file_id" ) };
	const auto hash { request->getOptionalParameter< std::string >( "hash" ) };

	if ( hash.has_value() )
	{
		auto db { drogon::app().getDbClient() };
		const auto sha256 { SHA256::fromHex( hash.value() ) };

		if ( !sha256.has_value() ) co_return sha256.error();

		const auto record_result {
			co_await db->execSqlCoro( "SELECT record_id FROM records WHERE sha256 = $1", sha256->toVec() )
		};

		if ( record_result.empty() ) co_return createNotFound( "No record with hash {} found", hash.value() );

		file_id = record_result[ 0 ][ "record_id" ].as< RecordID >();
	}

	if ( !file_id.has_value() && !hash.has_value() ) co_return createBadRequest( "No hash of file_id specified" );

	const RecordID id { file_id.value() };

	request->setPath( std::format( "/records/{}/file", id ) );

	co_return co_await drogon::app().forwardCoro( request );
}

} // namespace idhan::hyapi
