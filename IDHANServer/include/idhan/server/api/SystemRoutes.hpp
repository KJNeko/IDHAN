#pragma once

#include "ApiRouteGroup.hpp"

namespace idhan::server::api
{

class SystemRoutes final : public ApiRouteGroup
{
  public:

	void addRoutes( http::RouteRegistry& ) const override;
	[[nodiscard]] static http::HTTPTask< http::Response > index( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > status( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > health( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > api( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > version( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > log( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > generateApiKey( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > verifyKey( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > heartbeat( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > listLayouts( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > createLayout( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > getLayout( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > putLayout( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > deleteLayout( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > listPlugins( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > staticFile( http::RequestContext );
};

} // namespace idhan::server::api
