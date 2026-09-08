#pragma once

#include <IDHANDownloader/SecretProvider.hpp>
#include <drogon/utils/coroutine.h>

#include <expected>
#include <unordered_map>

namespace idhan::downloader
{

class DatabaseSecrets final : public SecretProvider
{
  public:

	std::optional< std::string > secret( std::string_view name ) override;
	[[nodiscard]] drogon::Task< std::expected< std::unordered_map< std::string, std::string >, std::string > > values()
		const;
	[[nodiscard]] drogon::Task< std::expected< void, std::string > > set(
		std::unordered_map< std::string, std::string > values );
};

} // namespace idhan::downloader
