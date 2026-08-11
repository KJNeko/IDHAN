//
// Created by kj16609 on 7/17/26.
//
#pragma once

#include <algorithm>
#include <array>
#include <string_view>

namespace idhan::api
{

/**
 * @brief Root path prefixes owned by the API rather than the WebUI.
 *
 * The SPA history fallback (see ServerContext::setupSPAFallback) serves index.html for unmatched
 * paths so client-side routes survive a reload. These prefixes are excluded from that fallback: a
 * request to a mistyped API path must return a real 404 instead of a page of HTML.
 *
 * @warning This list is mirrored in IDHANWeb/src/api/prefixes.ts, which drives the Vite dev proxy.
 * Adding a route family here without adding it there means the endpoint 404s in dev but works in
 * production. tests/src/api/apiPrefixes.cpp asserts the two agree.
 */
constexpr std::array api_prefixes { std::to_array< std::string_view >(
	{ "/api",    "/auth",      "/clusters", "/db",        "/embeddings",    "/file",    "/generate_api_key",
	  "/health", "/heartbeat", "/hyapi",    "/integrity", "/jobs",          "/layouts", "/log",
	  "/mime",   "/plugins",   "/purge",    "/records",   "/relationships", "/search",  "/tags",
	  "/test",   "/version" } ) };

/**
 * @brief True when @p path belongs to the API and must never fall back to the SPA.
 *
 * Matches on segment boundaries, so "/searching" is a SPA route while "/search" and "/search/foo"
 * are API paths.
 */
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
