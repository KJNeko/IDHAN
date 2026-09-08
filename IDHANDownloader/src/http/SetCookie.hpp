#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace idhan::downloader
{

struct SetCookie
{
	std::string name {};
	std::string value {};
	std::string domain {};
	std::string path {};
	bool secure {};
	bool http_only {};
	std::optional< std::chrono::system_clock::time_point > expires {};
	std::optional< std::int64_t > max_age {};
};

} // namespace idhan::downloader
