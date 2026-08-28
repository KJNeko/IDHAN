#include "api/APIMaintenance.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "MimeIDs.hpp"
#include "metadata/metadata.hpp"
#include "mime/prescan.hpp"
#include "modules/ModuleLoader.hpp"

namespace idhan::api
{

drogon::Task< drogon::HttpResponsePtr > APIMaintenance::createThumbnail( drogon::HttpRequestPtr request )
{
	if ( request->contentType() != drogon::CT_APPLICATION_OCTET_STREAM )
		co_return createBadRequest(
			"Content type must be octet-stream was {}", static_cast< int >( request->contentType() ) );

	const auto request_data { request->getBody() };

	if ( request_data.empty() ) co_return createBadRequest( "No data provided in POST request" );

	const auto mime_id { co_await mime::prescanMime( mime::MimeReader { request_data } ) };

	if ( mime_id == mime_ids::UNKNOWN ) co_return createBadRequest( "Failed to detect mime type" );

	const auto metadata_parser { modules::ModuleLoader::instance().getParserFor( mime_id ) };
	if ( metadata_parser.empty() ) co_return createInternalError( "Was unable to find parser for mime id {}", mime_id );

	const auto input_e { modules::CallInput::sharedForBytes(
		{ reinterpret_cast< const std::byte* >( request_data.data() ), request_data.size() } ) };

	if ( !input_e )
		co_return createInternalError( "Could not stage the request body for a module: {}", input_e.error() );

	const auto input { *input_e };

	modules::RemoteCallData call_data { .input = input, .mime_id = mime_id, .extra = {}, .depth = 0 };
	const auto metadata_json { co_await metadata_parser[ 0 ]->parseFile( call_data ) };

	if ( !metadata_json )
		co_return createInternalError(
			"Unable to parse metadata for mime id {} Reason: {}", mime_id, metadata_json.error() );

	call_data.extra = metadata_json->m_extra;

	if ( const auto* archive { std::get_if< MetadataInfoArchive >( &metadata_json->m_metadata ) } )
		metadata::applyArchiveEntries( call_data.extra, *archive );

	auto thumbnailers { modules::ModuleLoader::instance().getThumbnailerFor( mime_id ) };

	if ( thumbnailers.empty() ) co_return createNotFound( "No thumbnailer available for mime id {}", mime_id );

	const auto thumbnail_data { co_await thumbnailers.at( 0 )->createThumbnailFile( call_data, 128, 128 ) };

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
