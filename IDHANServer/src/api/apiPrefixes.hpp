#pragma once

#include <algorithm>
#include <array>
#include <string_view>

namespace idhan::api
{

//! API-owned prefixes excluded from SPA fallback. Keep mirrored with IDHANWeb/src/api/prefixes.ts.
constexpr std::array api_prefixes { std::to_array< std::string_view >(
	{ "/api",
	  "/auth",
	  "/clusters",
	  "/db",
	  "/download_sessions",
	  "/downloader",
	  "/embeddings",
	  "/file",
	  "/generate_api_key",
	  "/health", "/heartbeat", "/hyapi",    "/integrity", "/jobs",          "/layouts", "/log",
	  "/mime",
	  "/plugins",
	  "/purge",
	  "/rate_limits",
	  "/records",
	  "/relationships",
	  "/search",
	  "/tags",
	  "/test",   "/version" } ) };

//! Segment-boundary match: "/searching" is a SPA route, "/search/foo" is API.
[[nodiscard]] inline bool isApiPath( const std::string_view path )
{
	return std::ranges::any_of(
		api_prefixes,
		[ path ]( const std::string_view prefix )
		{
			if ( !path.starts_with( prefix ) ) return false;
			// Exact match, or the next character starts a new segment.
			return path.size() == prefix.size() || path[ prefix.size() ] == '/';
		} );
}

} // namespace idhan::api
