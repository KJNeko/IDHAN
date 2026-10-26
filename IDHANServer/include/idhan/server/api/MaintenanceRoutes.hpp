#pragma once

#include "ApiRouteGroup.hpp"

namespace idhan::server::api
{

class MaintenanceRoutes final : public ApiRouteGroup
{
  public:

	void addRoutes( http::RouteRegistry& ) const override;
	[[nodiscard]] static http::HTTPTask< http::Response > rescanMetadata( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > storageStats( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > databaseStats( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > parseMime( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > generateThumbnail( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > listMimes( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > integrity( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > purgeThumbnails( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > jobStatus( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > jobsStatus( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > listEmbeddingModels( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > generateEmbedding( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > searchEmbedding( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > compareEmbedding( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > deleteEmbeddingModel( http::RequestContext );
};

} // namespace idhan::server::api
