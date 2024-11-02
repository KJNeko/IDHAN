//
// Created by kj16609 on 11/2/24.
//

#include "checkContentType.hpp"

namespace idhan
{
	//! Responds with that the content type is unsupported or unknown
	void checkContentType(
		const drogon::HttpRequestPtr& request,
		const ResponseFunction& callback,
		const std::vector< drogon::ContentType > expected )
	{
		for ( const auto& item : expected )
			if ( request->contentType() == item ) return;

		Json::Value json {};
		auto& error_data = json[ "error" ];

		error_data[ "code" ] = 415; // Unsupported Media Type
		error_data[ "message" ] = "Content-Type did not match expected content";

		callback( drogon::HttpResponse::newHttpJsonResponse( json ) );
	}
} // namespace idhan