#pragma once

#include <IDHANDownloader/CookiePersistence.hpp>

namespace idhan::downloader
{

class DatabaseCookies final : public CookiePersistence
{
  public:

	std::vector< PersistedCookie > loadAll() override;
	void upsert( const PersistedCookie& cookie ) override;
	void erase( std::string_view name, std::string_view domain, std::string_view path ) override;
	void pruneExpired() override;
};

} // namespace idhan::downloader
