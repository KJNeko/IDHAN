#pragma once

#include "ApiRouteGroup.hpp"

namespace idhan::server::api
{

class HydrusRoutes final : public ApiRouteGroup
{
  public:

	void addRoutes( http::RouteRegistry& ) const override;
	[[nodiscard]] static http::HTTPTask< http::Response > apiVersion( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > verifyAccessKey( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > getService( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > getServices( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > clientInfo( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > searchFiles( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > fileMetadata( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > file( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > thumbnail( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > associateUrl( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > urlInfo( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > searchTags( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > clientOptions( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > popups( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > pages( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > notImplemented( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > gone( http::RequestContext );
};

} // namespace idhan::server::api
