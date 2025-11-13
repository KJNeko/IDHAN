//
// Created by kj16609 on 10/21/25.
//

#include "api/APIMaintenance.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "mime/MimeDatabase.hpp"
#include "modules/ModuleLoader.hpp"

namespace idhan::api
{

drogon::Task< drogon::HttpResponsePtr > APIMaintenance::createThumbnail( drogon::HttpRequestPtr request )
{
	if ( request->contentType() != drogon::CT_APPLICATION_OCTET_STREAM )
		co_return createBadRequest(
			"Content type must be octet-stream was {}", static_cast< int >( request->contentType() ) );
	const auto request_data { request->getBody() };

	if ( request_data.empty() )
	{
		Json::Value error;
		error[ "error" ] = "No data provided in POST request";
		co_return drogon::HttpResponse::newHttpJsonResponse( error );
	}

	const auto mime_str { co_await mime::getMimeDatabase()->scan( request_data ) };

	if ( !mime_str )
	{
		Json::Value response;
		response[ "success" ] = false;
		response[ "error" ] = "Failed to parse mime type";
		co_return drogon::HttpResponse::newHttpJsonResponse( response );
	}

	auto thumbnailers { modules::ModuleLoader::instance().getThumbnailerFor( *mime_str ) };

	if ( thumbnailers.empty() )
	{
		Json::Value response;
		response[ "success" ] = false;
		response[ "error" ] = "Failed to find thumbnailer for mime";
		co_return drogon::HttpResponse::newHttpJsonResponse( response );
	}

	//grab first thumbnailer
	auto thumbnailer { thumbnailers.at( 0 ) };

	const auto thumbnail_data {
		thumbnailer->createThumbnail( request_data.data(), request_data.size(), 128, 128, *mime_str )
	};

	if ( !thumbnailer )
	{
		Json::Value response;
		response[ "success" ] = false;
		response[ "error" ] = "Failed to parse thumbnail type";
		co_return drogon::HttpResponse::newHttpJsonResponse( response );
	}

	const auto& thumb_info { *thumbnail_data };

	auto response = drogon::HttpResponse::newHttpResponse();
	response->setContentTypeCode( drogon::CT_IMAGE_PNG );
	response->setBody(
		std::string( reinterpret_cast< const char* >( thumb_info.data.data() ), thumb_info.data.size() ) );
	co_return response;
}

} // namespace idhan::api
