#pragma once

#include <cstdint>

namespace idhan::downloader
{

struct RequestRate
{
	std::uint64_t requests { 1 };
	std::uint64_t seconds { 5 };

	bool operator==( const RequestRate& ) const = default;
};

} // namespace idhan::downloader
