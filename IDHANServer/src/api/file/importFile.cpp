//
// Created by kj16609 on 11/15/24.
//

#include <strstream>

#include "api/IDHANFileAPI.hpp"
#include "core/files/mime.hpp"
#include "crypto/sha256.hpp"
#include "fgl/defines.hpp"
#include "logging/log.hpp"
#include "mime/MimeDatabase.hpp"

namespace idhan::api
{

drogon::Task< drogon::HttpResponsePtr > IDHANFileAPI::importFile( const drogon::HttpRequestPtr request )
{
	log::debug( "Hit" );
	FGL_ASSERT( request, "Request invalid" );
	const auto request_data { request->getBody() };
	const auto content_type { request->getContentType() };
	log::debug( "Body length: {}", request_data.size() );
	log::debug( "Content type: {}", static_cast< int >( content_type ) );

	auto db { drogon::app().getDbClient() };

	const SHA256 sha256 { SHA256::hash( request_data ) };

	const std::optional< std::string > mime_str { mime::getInstance()->scan( request_data ) };

	log::debug( "MIME type: {}", mime_str.value_or( "NONE" ) );

	Json::Value root {};

	const auto response { drogon::HttpResponse::newHttpJsonResponse( root ) };

	co_return response;
}
} // namespace idhan::api
