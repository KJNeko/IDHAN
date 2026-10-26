#pragma once

#include <IDHANDownloader/SecretProvider.hpp>

#include <expected>
#include <unordered_map>

#include "idhan/runtime/Task.hpp"

namespace idhan::downloader
{

class DatabaseSecrets final : public SecretProvider
{
  public:

	std::optional< std::string > secret( std::string_view name ) override;
	[[nodiscard]] runtime::Task< std::expected< std::unordered_map< std::string, std::string >, std::string > > values()
		const;
	[[nodiscard]] runtime::Task< std::expected< void, std::string > > set(
		std::unordered_map< std::string, std::string > values );
};

} // namespace idhan::downloader
