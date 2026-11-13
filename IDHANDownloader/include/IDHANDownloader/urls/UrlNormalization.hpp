#pragma once

#include <ada.h>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace idhan::downloader
{

//! Rules a URL's classes apply on top of the default query sorting.
struct UrlRules
{
	bool sort_query { true };
	std::vector< std::string > remove_query {};
	std::vector< std::string > ephemeral_query {};
};

struct NormalizedUrl
{
	std::string url {};
	//! Ephemeral query fields discarded from url, in their original order and encoding. Currently unused.
	std::string ephemeral_query {};
};

//! Prefixes "https://" when the URL has no scheme, e.g. "example.com/post" or "//example.com/post".
[[nodiscard]] std::string withDefaultScheme( std::string_view url );

[[nodiscard]] std::expected< ada::url_aggregator, std::string > parseUrl( std::string_view url );

//! Merges each run of '/' in the path into one, e.g. "https://img4.gelbooru.com//images/a.jpeg"
//! becomes "https://img4.gelbooru.com/images/a.jpeg". ada keeps empty path segments.
[[nodiscard]] ada::url_aggregator collapsePathSlashes( ada::url_aggregator url );

//! Only ScriptRegistry::normalizeUrl should call this, so every URL class rule is applied.
//! Query fields keep their encoding; sorting is stable, preserving the order of repeated names.
[[nodiscard]] NormalizedUrl applyUrlRules( ada::url_aggregator url, const UrlRules& rules );

} // namespace idhan::downloader
