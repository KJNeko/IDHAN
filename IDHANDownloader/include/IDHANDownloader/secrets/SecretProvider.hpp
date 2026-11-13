#pragma once

#include <json/value.h>

#include <expected>
#include <string>
#include <string_view>

namespace idhan::downloader
{

class SecretProvider
{
  public:

	virtual ~SecretProvider() = default;

	//! Reads an in-memory snapshot for this configured domain, or an empty object if absent.
	//! Script workers call this directly: implementations must not perform I/O or wait on database work.
	virtual std::expected< Json::Value, std::string > secrets( std::string_view domain ) = 0;
};

} // namespace idhan::downloader
