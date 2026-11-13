#pragma once

#include <proxygen/lib/http/HTTPHeaders.h>

#include <cstdint>

namespace idhan::http
{

template < std::uint16_t Status = 204 >
struct EmptyResponse
{
	static_assert( Status >= 200 && Status <= 599, "A response must have a final HTTP status" );
	static constexpr auto status { Status };

	proxygen::HTTPHeaders headers {};
};

} // namespace idhan::http
