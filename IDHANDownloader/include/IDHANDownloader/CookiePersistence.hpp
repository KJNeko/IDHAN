#pragma once

#include <chrono>
#include <string>
#include <string_view>
#include <vector>

namespace idhan::downloader
{

struct PersistedCookie
{
	std::string name {};
	std::string domain {};
	std::string path {};
	std::string value {};
	bool secure {};
	bool host_only {};
	std::chrono::system_clock::time_point expires {};
};

class CookiePersistence
{
  public:

	virtual ~CookiePersistence() = default;

	virtual std::vector< PersistedCookie > loadAll() = 0;
	virtual void upsert( const PersistedCookie& cookie ) = 0;
	virtual void erase( std::string_view name, std::string_view domain, std::string_view path ) = 0;
	virtual void pruneExpired() = 0;
};

} // namespace idhan::downloader
