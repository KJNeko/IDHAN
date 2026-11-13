#pragma once

#include <IDHANDownloader/secrets/SecretProvider.hpp>

#include <expected>
#include <atomic>
#include <memory>
#include <folly/coro/Mutex.h>
#include <unordered_map>

#include "idhan/runtime/Task.hpp"

namespace idhan::downloader
{

class DatabaseSecrets final : public SecretProvider
{
	struct Snapshot
	{
		std::unordered_map< std::string, Json::Value > documents {};
		std::unordered_map< std::string, std::string > serialized {};
	};

	std::atomic< std::shared_ptr< const Snapshot > > m_cache {};
	folly::coro::Mutex m_update_mutex {};

  public:

	std::expected< Json::Value, std::string > secrets( std::string_view domain ) override;
	// Refreshes one complete snapshot, retaining the previous values on failure.
	[[nodiscard]] runtime::Task< std::expected< void, std::string > > reload();
	// Map values are serialized JSON objects, as returned by PostgreSQL jsonb::text.
	[[nodiscard]] runtime::Task< std::expected< std::unordered_map< std::string, std::string >, std::string > > values()
		const;
	[[nodiscard]] runtime::Task< std::expected< void, std::string > > set(
		std::unordered_map< std::string, std::string > values );
};

} // namespace idhan::downloader
