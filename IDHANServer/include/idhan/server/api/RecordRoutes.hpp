#pragma once

#include "ApiRouteGroup.hpp"

namespace idhan::server::api
{

class RecordRoutes final : public ApiRouteGroup
{
  public:

	void addRoutes( http::RouteRegistry& ) const override;

	[[nodiscard]] static http::HTTPTask< http::Response > importFile( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > createRecord( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > fetchMetadataBatch( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > searchHash( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > randomActive( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > notes( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > addNote( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > removeNote( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > urls( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > addUrls( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > removeUrls( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > addTags( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > removeTags( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > listTags( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > listActiveTags( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > listActiveTagsVerbose( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > thumbnail( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > file( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > info( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > scanMetadata( http::RequestContext );
};

} // namespace idhan::server::api
