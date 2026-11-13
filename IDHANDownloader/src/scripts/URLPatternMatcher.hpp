#pragma once

#include <json/value.h>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace ada
{
class url_search_params;
}

namespace idhan::downloader
{

//! Immutable route definition. Each calling thread owns its runtime and compiled regexes.
class URLPatternMatcher
{
	class Impl;
	std::unique_ptr< Impl > m_impl;
	explicit URLPatternMatcher( std::unique_ptr< Impl > impl );

  public:

	~URLPatternMatcher();
	[[nodiscard]] static std::expected< std::shared_ptr< URLPatternMatcher >, std::string > create(
		const Json::Value& definition );
	[[nodiscard]] std::expected< std::optional< Json::Value >, std::string > match(
		std::string_view url,
		ada::url_search_params& parameters ) const;
};

[[nodiscard]] std::expected< std::optional< Json::Value >, std::string > matchURLPattern(
	const Json::Value& definition,
	std::string_view url );
}
