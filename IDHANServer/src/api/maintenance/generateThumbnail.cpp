//
// Created by kj16609 on 10/21/25.
//

#include "api/APIMaintenance.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "mime/MimeDatabase.hpp"
#include "modules/ModuleLoader.hpp"
#include "profiling/tracy.hpp"

namespace idhan::api
{

drogon::Task< drogon::HttpResponsePtr > APIMaintenance::createThumbnail( drogon::HttpRequestPtr request )
{
	if ( request->contentType() != drogon::CT_APPLICATION_OCTET_STREAM )
		co_return createBadRequest(
			"Content type must be octet-stream was {}", static_cast< int >( request->contentType() ) );

	const auto request_data { request->getBody() };

	if ( request_data.empty() ) co_return createBadRequest( "No data provided in POST request" );

	//TODO: Create handle for multipart to get filenames
	const auto mime_str { co_await mime::getMimeDatabase()->scan( request_data, "" ) };

	if ( !mime_str ) co_return createBadRequest( "Failed to detect mime type" );

	const auto metadata_parser { modules::ModuleLoader::instance().getParserFor( *mime_str ) };
	if ( metadata_parser.empty() ) co_return createInternalError( "Was unable to find parser for {}", *mime_str );

	idhan::data_view data_view { reinterpret_cast< const std::uint8_t* >( request_data.data() ), request_data.size() };
	ModuleCallData call_data { .file_view = data_view, .mime_name = *mime_str, .extra = {} };
	const auto metadata_json { metadata_parser[ 0 ]->parseFile( call_data ) };

	if ( !metadata_json )
		co_return createInternalError(
			"Unable to parse metadata for mime {} Reason: {}", *mime_str, metadata_json.error() );

	call_data.extra = metadata_json->m_extra;

	auto thumbnailers { modules::ModuleLoader::instance().getThumbnailerFor( *mime_str ) };

	if ( thumbnailers.empty() ) co_return createNotFound( "No thumbnailer available for mime type {}", *mime_str );

	ZoneScopedN( "module::thumbnail" );
	const auto thumbnail_data { thumbnailers.at( 0 )->createThumbnailFile( call_data, 128, 128 ) };

	if ( !thumbnail_data ) co_return createInternalError( thumbnail_data.error() );

	const auto& thumb_info { *thumbnail_data };

	auto response { drogon::HttpResponse::newHttpResponse() };
	response->setContentTypeCode( drogon::CT_IMAGE_WEBP );
	response->setBody(
		std::string(
			reinterpret_cast< const char* >( thumb_info.m_pixel_data.data() ), thumb_info.m_pixel_data.size() ) );
	co_return response;
}

} // namespace idhan::api
