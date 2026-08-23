#include "api/APIMaintenance.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "MimeIDs.hpp"
#include "mime/guessExtension.hpp"
#include "mime/prescan.hpp"
#include "mime/refineMime.hpp"
#include "modules/ModuleLoader.hpp"

namespace idhan::api
{

drogon::Task< Json::Value > processMetadata( const MimeID mime_id, const std::string_view request_data )
{
	Json::Value response { Json::arrayValue };
	auto metadata_modules { modules::ModuleLoader::instance().getParserFor( mime_id ) };

	const auto input_e { modules::CallInput::sharedForBytes(
		{ reinterpret_cast< const std::byte* >( request_data.data() ), request_data.size() } ) };

	if ( !input_e )
	{
		Json::Value failure {};
		failure[ "error" ] = std::format( "Could not stage the request body for a module: {}", input_e.error() );
		response.append( std::move( failure ) );
		co_return response;
	}

	const auto input { *input_e };

	for ( const auto& metadata_module : metadata_modules )
	{
		Json::Value metadata_obj {};
		metadata_obj[ "name" ] = std::string( metadata_module->name() );

		const modules::RemoteCallData data { .input = input, .mime_id = mime_id, .extra = {}, .depth = 0 };

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
			metadata_obj[ "count" ] = info.contained_records.size();
			metadata_obj[ "uncompressed_size" ] = info.m_size;
		}

		metadata_obj[ "extra" ] = metadata_info->m_extra;

		response.append( std::move( metadata_obj ) );
	}

	co_return response;
}

//! Refines \p generic_id against whichever modules handle it, recording both ids on \p response.
drogon::Task< void > describeMimeID(
	Json::Value& response,
	MimeID& mime_id,
	const MimeID generic_id,
	const std::string_view data )
{
	response[ "generic_mime_id" ] = generic_id;
	response[ "mime_id" ] = generic_id;
	mime_id = generic_id;

	auto input {
		modules::CallInput::sharedForBytes( { reinterpret_cast< const std::byte* >( data.data() ), data.size() } )
	};

	if ( !input ) co_return;

	mime_id = co_await mime::refineMimeID( generic_id, std::move( *input ) );
	response[ "mime_id" ] = mime_id;
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

	const auto scanned_id { co_await mime::prescanMime( mime::MimeReader { request_data } ) };

	const auto generic_id { mime::guessMimeFromExtension(
		scanned_id, request->getOptionalParameter< std::string >( "filename" ).value_or( "" ) ) };

	Json::Value response;

	if ( generic_id == mime_ids::UNKNOWN )
	{
		response[ "success" ] = false;
		response[ "error" ] = "Failed to parse mime type";
		co_return drogon::HttpResponse::newHttpJsonResponse( response );
	}

	response[ "success" ] = true;
	response[ "mime" ] = std::string { mime_ids::mime_names.at( generic_id ) };

	MimeID mime_id { mime_ids::INVALID };
	co_await describeMimeID( response, mime_id, generic_id, request_data );

	response[ "metadata_modules" ] = co_await processMetadata( mime_id, request_data );

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

	const auto data { file.fileData() };
	const auto size { file.fileLength() };

	const std::string_view file_data { data, size };

	const auto scanned_id { co_await mime::prescanMime( mime::MimeReader { file_data } ) };

	const auto generic_id { mime::guessMimeFromExtension( scanned_id, file.getFileName() ) };

	Json::Value response;

	if ( generic_id == mime_ids::UNKNOWN )
	{
		response[ "success" ] = false;
		response[ "error" ] = "Failed to parse mime type";
		co_return drogon::HttpResponse::newHttpJsonResponse( response );
	}

	response[ "success" ] = true;
	response[ "mime" ] = std::string { mime_ids::mime_names.at( generic_id ) };

	MimeID mime_id { mime_ids::INVALID };
	co_await describeMimeID( response, mime_id, generic_id, file_data );

	response[ "metadata_modules" ] = co_await processMetadata( mime_id, file_data );

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

} // namespace idhan::api
