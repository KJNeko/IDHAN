#pragma once

#include <folly/coro/Task.h>

#include <chrono>
#include <string>
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

	virtual folly::coro::Task< std::vector< PersistedCookie > > loadAll() = 0;
	virtual folly::coro::Task< void > upsert( PersistedCookie cookie ) = 0;
	virtual folly::coro::Task< void > erase( std::string name, std::string domain, std::string path ) = 0;

	virtual folly::coro::Task< void > pruneExpired() = 0;
};

} // namespace idhan::downloader
