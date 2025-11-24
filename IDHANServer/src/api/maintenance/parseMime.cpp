//
// Created by kj16609 on 10/21/25.
//

#include "api/APIMaintenance.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "mime/MimeDatabase.hpp"
#include "modules/ModuleLoader.hpp"

namespace idhan::api
{

drogon::Task< drogon::HttpResponsePtr > parseMimeOctet( drogon::HttpRequestPtr request )
{
	const auto request_data { request->getBody() };

	if ( request_data.empty() )
	{
		Json::Value error;
		error[ "error" ] = "No data provided in POST request";
		co_return drogon::HttpResponse::newHttpJsonResponse( error );
	}

	const auto mime_str { co_await mime::getMimeDatabase()->scan( request_data, "" ) };

	Json::Value response;

	if ( !mime_str )
	{
		response[ "success" ] = false;
		response[ "error" ] = "Failed to parse mime type";
		co_return drogon::HttpResponse::newHttpJsonResponse( response );
	}

	response[ "success" ] = true;
	response[ "mime" ] = mime_str.value();

	auto metadata_modules { modules::ModuleLoader::instance().getParserFor( *mime_str ) };

	response[ "metadata_modules" ] = {};

	for ( const auto& metadata_module : metadata_modules )
	{
		Json::Value metadata_obj {};
		metadata_obj[ "name" ] = std::string( metadata_module->name() );

		auto metadata_info { metadata_module->parseFile( request_data.data(), request_data.size(), *mime_str ) };

		response[ "metadata_modules" ].append( std::move( metadata_obj ) );
	}

	co_return drogon::HttpResponse::newHttpJsonResponse( response );
}

drogon::Task< drogon::HttpResponsePtr > parseMimeMultiform( drogon::HttpRequestPtr request )
{
	const auto request_data { request->getBody() };

	if ( request_data.empty() )
	{
		Json::Value error;
		error[ "error" ] = "No data provided in POST request";
		co_return drogon::HttpResponse::newHttpJsonResponse( error );
	}

	drogon::MultiPartParser parser {};
	parser.parse( request );

	const auto files { parser.getFiles() };

	if ( files.size() != 1 ) co_return createBadRequest( "Only 1 file must be provided in the multipart" );

	const auto file { files.at( 0 ) };
	const auto file_name { file.getFileName() };

	const auto data { file.fileData() };
	const auto size { file.fileLength() };

	const std::string_view file_data { data, size };

	const auto mime_str { co_await mime::getMimeDatabase()->scan( file_data, file_name ) };

	Json::Value response;

	if ( !mime_str )
	{
		response[ "success" ] = false;
		response[ "error" ] = "Failed to parse mime type";
		co_return drogon::HttpResponse::newHttpJsonResponse( response );
	}

	response[ "success" ] = true;
	response[ "mime" ] = mime_str.value();

	auto metadata_modules { modules::ModuleLoader::instance().getParserFor( *mime_str ) };

	response[ "metadata_modules" ] = {};

	for ( const auto& metadata_module : metadata_modules )
	{
		Json::Value metadata_obj {};
		metadata_obj[ "name" ] = std::string( metadata_module->name() );

		auto metadata_info { metadata_module->parseFile( request_data.data(), request_data.size(), *mime_str ) };

		response[ "metadata_modules" ].append( std::move( metadata_obj ) );
	}

	co_return drogon::HttpResponse::newHttpJsonResponse( response );
}

drogon::Task< drogon::HttpResponsePtr > APIMaintenance::parseMime( drogon::HttpRequestPtr request )
{
	if ( request->contentType() == drogon::CT_APPLICATION_OCTET_STREAM )
	{
		co_return co_await parseMimeOctet( request );
	}
	else if ( request->contentType() == drogon::CT_MULTIPART_FORM_DATA )
	{
		co_return co_await parseMimeMultiform( request );
	}

	co_return createBadRequest(
		"Content type must be application/octet-stream or multipart/form-data was {}",
		static_cast< int >( request->contentType() ) );
}

drogon::Task< drogon::HttpResponsePtr > APIMaintenance::reloadMime( drogon::HttpRequestPtr request )
{
	co_await mime::getMimeDatabase()->reloadMimeParsers();

	co_return co_await listParsers( request );
}

drogon::Task< drogon::HttpResponsePtr > APIMaintenance::listParsers( [[maybe_unused]] drogon::HttpRequestPtr request )
{
	const auto mime_db { idhan::mime::getMimeDatabase() };

	co_return drogon::HttpResponse::newHttpJsonResponse( mime_db->dump() );
}

} // namespace idhan::api
