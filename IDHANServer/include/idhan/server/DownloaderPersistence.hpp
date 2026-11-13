#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "IDHANDownloader/cookies/CookiePersistence.hpp"
#include "IDHANDownloader/secrets/SecretProvider.hpp"
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
	[[nodiscard]] runtime::Task<> upsert( downloader::PersistedCookie cookie ) override;
	[[nodiscard]] runtime::Task<> erase( std::string name, std::string domain, std::string path ) override;
	void pruneExpired() override;
	[[nodiscard]] std::expected< Json::Value, std::string > secrets( std::string_view domain ) override;
	[[nodiscard]] runtime::Task<> replaceSecrets( std::string domain, Json::Value value );
};
} // namespace idhan::server
