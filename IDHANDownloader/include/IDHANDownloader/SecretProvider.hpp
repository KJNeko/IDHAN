#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace idhan::downloader
{

class SecretProvider
{
  public:

	virtual ~SecretProvider() = default;

	virtual std::optional< std::string > secret( std::string_view name ) = 0;
};

} // namespace idhan::downloader
