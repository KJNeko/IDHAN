#pragma once

#include "ApiRouteGroup.hpp"

namespace idhan::server::api
{

class SearchRoutes final : public ApiRouteGroup
{
  public:

	void addRoutes( http::RouteRegistry& ) const override;
	[[nodiscard]] static http::HTTPTask< http::Response > search( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > tagInfo( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > tagRelationships( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > tagSearch( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > autocomplete( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > createTags( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > createTagDomain( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > listTagDomains( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > tagDomainInfo( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > deleteTagDomain( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > createParents( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > createAliases( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > createSiblings( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > removeParents( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > removeAliases( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > removeSiblings( http::RequestContext );
};

} // namespace idhan::server::api
