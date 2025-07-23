//
// Created by kj16609 on 3/6/25.
//

#include "createBadRequest.hpp"

namespace idhan::internal
{

drogon::HttpResponsePtr createBadResponse( const std::string& message, drogon::HttpStatusCode code )
{
	Json::Value out_json {};
	out_json[ "error" ] = message;
	out_json[ "status" ] = static_cast< Json::Value::Int >( code );

	auto response { drogon::HttpResponse::newHttpJsonResponse( out_json ) };
	response->setStatusCode( code );

	return response;
}

} // namespace idhan::internal