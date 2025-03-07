//
// Created by kj16609 on 3/6/25.
//

#include "createBadRequest.hpp"

namespace idhan
{

namespace internal
{

drogon::HttpResponsePtr createBadResponse( const std::string& message, drogon::HttpStatusCode code )
{
	Json::Value out_json {};
	out_json[ "error" ] = message;
	out_json[ "status" ] = code;

	auto response { drogon::HttpResponse::newHttpJsonResponse( out_json ) };
	response->setStatusCode( code );

	return response;
}

} // namespace internal

drogon::HttpResponsePtr createBadRequest( const Json::Value& json )
{
	if ( json[ "message" ].isString() )
		return internal::createBadResponse( json[ "message" ].asString(), drogon::HttpStatusCode::k400BadRequest );

	return drogon::HttpResponse::newHttpResponse( drogon::HttpStatusCode::k500InternalServerError, drogon::CT_NONE );
}

} // namespace idhan