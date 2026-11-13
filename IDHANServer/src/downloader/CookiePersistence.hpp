#pragma once

#include <IDHANDownloader/cookies/CookiePersistence.hpp>

namespace idhan::downloader
{

class DatabaseCookies final : public CookiePersistence
{
  public:

	folly::coro::Task< std::vector< PersistedCookie > > loadAll() override;
	folly::coro::Task< void > pruneExpired() override;
	folly::coro::Task< void > upsert( PersistedCookie cookie ) override;
	folly::coro::Task< void > erase( std::string name, std::string domain, std::string path ) override;
};

} // namespace idhan::downloader
