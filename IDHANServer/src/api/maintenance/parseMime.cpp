#include "api/APIMaintenance.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "mime/MimeDatabase.hpp"
#include "modules/ModuleLoader.hpp"

namespace idhan::api
{

drogon::Task< Json::Value > processMetadata( const std::string mime_str, const std::string_view request_data )
{
	// a default-constructed Json::Value is null until first append, so no matching
	// modules would serialize metadata_modules as null instead of []
	Json::Value response { Json::arrayValue };
	auto metadata_modules { modules::ModuleLoader::instance().getParserFor( mime_str ) };

	auto blob { ipc::Blob::fromBytes(
		std::span< const std::byte > {
			reinterpret_cast< const std::byte* >( request_data.data() ), request_data.size() } ) };

	if ( !blob )
	{
		// This helper hands back a JSON array, not a response, so the failure is reported as an entry
		// in that array rather than as an HTTP error.
		Json::Value failure {};
		failure[ "error" ] = std::format( "Could not stage the request body for a module: {}", blob.error() );
		response.append( std::move( failure ) );
		co_return response;
	}

	auto input_e { modules::CallInput::forBlob( std::move( blob.value() ) ) };

	if ( !input_e )
	{
		Json::Value failure {};
		failure[ "error" ] = std::format( "Could not stage the request body for a module: {}", input_e.error() );
		response.append( std::move( failure ) );
		co_return response;
	}

	const auto input { std::make_shared< const modules::CallInput >( std::move( *input_e ) ) };

	for ( const auto& metadata_module : metadata_modules )
	{
		Json::Value metadata_obj {};
		metadata_obj[ "name" ] = std::string( metadata_module->name() );

		const modules::RemoteCallData data { .input = input, .mime_name = mime_str, .extra = {}, .depth = 0 };

		auto metadata_info { co_await metadata_module->parseFile( data ) };

		if ( !metadata_info )
		{
			metadata_obj[ "error" ] = metadata_info.error();
			response.append( std::move( metadata_obj ) );
			continue;
		}

		const auto metadata { metadata_info->m_metadata };

		if ( std::holds_alternative< MetadataInfoArchive >( metadata ) )
		{
			const auto& info { std::get< MetadataInfoArchive >( metadata ) };
			metadata_obj[ "count" ] = info.contained_hashes.size();
			metadata_obj[ "uncompressed_size" ] = info.m_size;
		}

		metadata_obj[ "extra" ] = metadata_info->m_extra;

		response.append( std::move( metadata_obj ) );
	}

	co_return response;
}

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

	response[ "metadata_modules" ] = co_await processMetadata( *mime_str, request_data );

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

	response[ "metadata_modules" ] = co_await processMetadata( *mime_str, file_data );

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
	const auto reload_result { co_await mime::getMimeDatabase()->reloadMimeParsers() };

	// a swallowed reload failure would report the stale parser list as a success
	if ( !reload_result ) co_return reload_result.error();

	co_return co_await listParsers( request );
}

drogon::Task< drogon::HttpResponsePtr > APIMaintenance::listParsers( [[maybe_unused]] drogon::HttpRequestPtr request )
{
	const auto mime_db { idhan::mime::getMimeDatabase() };

	co_return drogon::HttpResponse::newHttpJsonResponse( mime_db->dump() );
}

} // namespace idhan::api
