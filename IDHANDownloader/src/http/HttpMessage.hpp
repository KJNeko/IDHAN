#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

class HttpHeaders
{
	std::vector< std::pair< std::string, std::string > > m_entries {};

  public:

	void add( std::string name, std::string value );
	void set( std::string name, std::string value );
	std::size_t erase( std::string_view name );

	[[nodiscard]] bool contains( std::string_view name ) const;
	[[nodiscard]] std::string_view get( std::string_view name ) const;
	[[nodiscard]] std::vector< std::string_view > all( std::string_view name ) const;

	[[nodiscard]] auto begin() const { return m_entries.begin(); }

	[[nodiscard]] auto end() const { return m_entries.end(); }

	[[nodiscard]] bool empty() const { return m_entries.empty(); }

	[[nodiscard]] std::size_t size() const { return m_entries.size(); }

	void clear() { m_entries.clear(); }
};

struct SetCookie
{
	std::string name {};
	std::string value {};
	std::string domain {};
	std::string path {};
	bool secure {};
	bool http_only {};
	std::optional< std::chrono::system_clock::time_point > expires {};
	std::optional< std::int64_t > max_age {};
};

[[nodiscard]] std::optional< SetCookie > parseSetCookie( std::string_view header );

} // namespace idhan::downloader
