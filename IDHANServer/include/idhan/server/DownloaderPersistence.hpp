#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "IDHANDownloader/CookiePersistence.hpp"
#include "IDHANDownloader/SecretProvider.hpp"
#include "idhan/runtime/Task.hpp"

namespace idhan::db
{
class DBContext;
}

namespace idhan::server
{
class DownloaderPersistence final : public downloader::CookiePersistence, public downloader::SecretProvider
{
	struct Impl;
	std::unique_ptr< Impl > m_impl;

  public:

	explicit DownloaderPersistence( db::DBContext& );
	~DownloaderPersistence() override;
	DownloaderPersistence( const DownloaderPersistence& ) = delete;
	DownloaderPersistence& operator=( const DownloaderPersistence& ) = delete;

	[[nodiscard]] runtime::Task<> load();
	[[nodiscard]] runtime::Task<> drain();
	[[nodiscard]] runtime::Task<> waitForPersistence();
	[[nodiscard]] std::vector< downloader::PersistedCookie > loadAll() override;
	void upsert( const downloader::PersistedCookie& ) override;
	void erase( std::string_view name, std::string_view domain, std::string_view path ) override;
	void pruneExpired() override;
	[[nodiscard]] std::optional< std::string > secret( std::string_view name ) override;
	[[nodiscard]] runtime::Task<> replaceSecret( std::string name, std::string value );
};
} // namespace idhan::server
