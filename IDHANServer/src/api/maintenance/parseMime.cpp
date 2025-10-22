//
// Created by kj16609 on 10/21/25.
//

#include "api/APIMaintenance.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "mime/MimeDatabase.hpp"

namespace idhan::api
{

drogon::Task< drogon::HttpResponsePtr > APIMaintenance::parseMime( drogon::HttpRequestPtr request )
{
	if ( request->contentType() != drogon::CT_APPLICATION_OCTET_STREAM )
		co_return createBadRequest( "Content type must be octet-stream" );
	const auto request_data { request->getBody() };

	if ( request_data.empty() )
	{
		Json::Value error;
		error[ "error" ] = "No data provided in POST request";
		co_return drogon::HttpResponse::newHttpJsonResponse( error );
	}

	const auto mime_str { co_await mime::getInstance()->scan( request_data ) };

	Json::Value response;

	if ( !mime_str )
	{
		response[ "success" ] = false;
		response[ "error" ] = "Failed to parse mime type";
		co_return drogon::HttpResponse::newHttpJsonResponse( response );
	}

	response[ "success" ] = true;
	response[ "mime" ] = mime_str.value();

	co_return drogon::HttpResponse::newHttpJsonResponse( response );
}

drogon::Task< drogon::HttpResponsePtr > APIMaintenance::reloadMime( drogon::HttpRequestPtr request )
{
	mime::getInstance()->reloadMimeParsers();

	co_return co_await listParsers( request );
}

drogon::Task< drogon::HttpResponsePtr > APIMaintenance::listParsers( [[maybe_unused]] drogon::HttpRequestPtr request )
{
	const auto mime_db { idhan::mime::getInstance() };

	co_return drogon::HttpResponse::newHttpJsonResponse( mime_db->dump() );
}

} // namespace idhan::api
