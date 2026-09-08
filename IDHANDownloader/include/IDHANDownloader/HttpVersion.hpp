#pragma once

#include <cstdint>

namespace idhan::downloader
{

//! Unsupported HTTP versions fall back to an available version.
enum class HttpVersion : std::uint8_t
{
	HTTP_1_1,
	HTTP_2,
	HTTP_3,
};

} // namespace idhan::downloader
