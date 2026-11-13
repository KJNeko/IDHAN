#pragma once

#include <cstdint>
#include <cstddef>

namespace idhan::downloader
{

struct RequestRate
{
	std::uint64_t requests { 1 };
	std::uint64_t seconds { 5 };
	bool spread {};
	std::size_t concurrency { 1 };

	bool operator==( const RequestRate& ) const = default;
};

} // namespace idhan::downloader
