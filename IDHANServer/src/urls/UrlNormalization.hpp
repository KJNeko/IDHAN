#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

#include "idhan/runtime/Task.hpp"

namespace idhan::helpers
{

enum class UrlNormalizationFailure
{
	//! The URL failed ada's parser or a URL class rule.
	InvalidUrl,
	Unavailable,
};

struct UrlNormalizationError
{
	UrlNormalizationFailure failure {};
	std::string message {};
};

[[nodiscard]] inline std::uint16_t httpStatus( const UrlNormalizationError& error )
{
	return error.failure == UrlNormalizationFailure::InvalidUrl ? 400 : 503;
}

//! Normalizes every URL through the downloader's URL classes, failing on the first invalid URL.
//! URLs stored or looked up in the urls table must come from here.
[[nodiscard]] runtime::Task< std::expected< std::vector< std::string >, UrlNormalizationError > > normalizeUrls(
	std::vector< std::string > urls );

[[nodiscard]] runtime::Task< std::expected< std::string, UrlNormalizationError > > normalizeUrl( std::string url );

//! Each URL as submitted plus its normalized form, for removing rows stored before they were normalized.
//! Invalid URLs are kept as submitted instead of failing.
[[nodiscard]] runtime::Task< std::expected< std::vector< std::string >, UrlNormalizationError > > urlSpellings(
	std::vector< std::string > urls );

} // namespace idhan::helpers
