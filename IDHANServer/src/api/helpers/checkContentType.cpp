#include "checkContentType.hpp"

namespace idhan::api::helpers
{
void checkContentType(
	const drogon::HttpRequestPtr& request,
	const ResponseFunction& callback,
	const std::vector< drogon::ContentType >& expected )
{
	for ( const auto& item : expected )
		if ( request->contentType() == item ) return;

	Json::Value json {};
	auto& error_data = json[ "error" ];

	error_data[ "code" ] = drogon::k415UnsupportedMediaType;
	error_data[ "message" ] = "Content-Type did not match expected content";

	callback( drogon::HttpResponse::newHttpJsonResponse( json ) );
}
} // namespace idhan::api::helpers
