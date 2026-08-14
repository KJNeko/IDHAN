#include "HyAPIAuth.hpp"

#include "constants/header-names.hpp"

namespace idhan::hyapi
{

HyAPIAuth::HyAPIAuth() = default;

drogon::Task< drogon::HttpResponsePtr > HyAPIAuth::doFilter( const drogon::HttpRequestPtr& req )
{
#ifdef IDHAN_DISABLE_API_AUTH
	co_return nullptr;
#else
	const auto param_key { req->getOptionalParameter< std::string >( HY_ACCESS_KEY_HEADER_NAME ) };
	const auto header_key { req->getHeader( HY_ACCESS_KEY_HEADER_NAME ) };
	const auto key { param_key ? *param_key : header_key };

	if ( key == "" )
	{
		Json::Value root;
		root[ "error" ] = "No access key or session key provided!";
		root[ "exception_type" ] = "MissingCredentialsException";
		root[ "status_code" ] = drogon::k401Unauthorized;

		co_return drogon::HttpResponse::newHttpJsonResponse( root );
	}

	req->addHeader( "IDHAN-API-Key", key );

	co_return nullptr;
#endif
}

} // namespace idhan::hyapi
