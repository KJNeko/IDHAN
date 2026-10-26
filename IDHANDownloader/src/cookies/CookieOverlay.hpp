#pragma once

#include <chrono>
#include <mutex>
#include <string_view>
#include <vector>

#include "Cookie.hpp"

namespace idhan::downloader
{

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

} // namespace idhan::downloader
