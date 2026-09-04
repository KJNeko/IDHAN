#pragma once

#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "IDHANDownloader/CookiePersistence.hpp"

namespace idhan::downloader
{
class HttpHeaders;

struct Cookie
{
	std::string name {};
	std::string value {};
	std::string domain {};
	std::string path { "/" };
	bool secure {};
	bool host_only {};
	std::optional< std::chrono::system_clock::time_point > expires { std::nullopt };

	[[nodiscard]] bool sameIdentity( const Cookie& other ) const
	{
		return name == other.name && domain == other.domain && path == other.path;
	}
};

[[nodiscard]] bool cookieDomainMatches( std::string_view host, std::string_view domain, bool host_only );
[[nodiscard]] bool cookiePathMatches( std::string_view request_path, std::string_view cookie_path );
[[nodiscard]] std::string defaultCookiePath( std::string_view request_path );

class CookieOverlay
{
	mutable std::mutex m_mutex {};
	std::vector< Cookie > m_cookies {};

  public:

	CookieOverlay() = default;
	CookieOverlay( const CookieOverlay& ) = delete;
	CookieOverlay& operator=( const CookieOverlay& ) = delete;

	void set( Cookie cookie );
	void erase( std::string_view name, std::string_view domain, std::string_view path );
	void appendMatching(
		std::string_view host,
		std::string_view path,
		bool secure,
		std::chrono::system_clock::time_point now,
		std::vector< Cookie >& out ) const;

	[[nodiscard]] std::vector< Cookie > cookies() const;
};

class CookieStore
{
	mutable std::mutex m_mutex {};
	std::vector< Cookie > m_cookies {};
	CookiePersistence* m_persistence {};

  public:

	explicit CookieStore( CookiePersistence* persistence = nullptr );
	CookieStore( const CookieStore& ) = delete;
	CookieStore& operator=( const CookieStore& ) = delete;

	void load();
	void set( Cookie cookie );
	void erase( std::string_view name, std::string_view domain, std::string_view path );
	void appendMatching(
		std::string_view host,
		std::string_view path,
		bool secure,
		std::chrono::system_clock::time_point now,
		std::vector< Cookie >& out ) const;

	[[nodiscard]] std::vector< Cookie > cookies() const;
};

void applyRequestCookies(
	std::string_view url,
	const CookieOverlay& overlay,
	const CookieStore& store,
	HttpHeaders& headers,
	std::chrono::system_clock::time_point now = std::chrono::system_clock::now() );

struct ResponseCookies
{
	std::vector< Cookie > persistent {};
	std::vector< Cookie > session {};
	std::vector< Cookie > removed {};
};

[[nodiscard]] ResponseCookies collectResponseCookies(
	std::string_view url,
	const HttpHeaders& headers,
	std::chrono::system_clock::time_point now = std::chrono::system_clock::now() );

} // namespace idhan::downloader
