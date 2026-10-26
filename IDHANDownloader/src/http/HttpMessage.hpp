#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "HttpHeaders.hpp"
#include "SetCookie.hpp"

namespace idhan::downloader
{

enum class HttpMethod : std::uint8_t
{
	GET,
	POST,
	HEAD,
	PUT,
	DELETE,
	OPTIONS,
	PATCH,
};

[[nodiscard]] std::optional< HttpMethod > parseHttpMethod( std::string_view method );
[[nodiscard]] std::string_view httpMethodName( HttpMethod method );

[[nodiscard]] bool isValidHttpHeaderName( std::string_view name );
[[nodiscard]] bool isValidHttpHeaderValue( std::string_view value );

//! Bodies past this are reported by size instead of being printed.
inline constexpr std::size_t HTTP_BODY_LOG_LIMIT { 2048 };

[[nodiscard]] bool isMarkupContentType( std::string_view content_type );

//! Whitespace is collapsed so a short body stays on one log line.
[[nodiscard]] std::string describeBody( std::string_view body, std::uint64_t bytes );

[[nodiscard]] std::optional< SetCookie > parseSetCookie( std::string_view header );

} // namespace idhan::downloader
