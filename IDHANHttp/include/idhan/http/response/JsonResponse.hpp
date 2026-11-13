#pragma once

#include <proxygen/lib/http/HTTPHeaders.h>

#include <cstdint>
#include <variant>

#include "idhan/http/response/Response.hpp"

namespace idhan::http
{

template < typename T, std::uint16_t Status = 200 >
struct JsonResponse
{
	static_assert( Status >= 200 && Status <= 599, "A response must have a final HTTP status" );
	static_assert( Status != 204 && Status != 205 && Status != 304, "Use EmptyResponse for a bodyless status" );
	using Value = T;
	static constexpr auto status { Status };

	T value {};
	proxygen::HTTPHeaders headers {};
};

//! The typed alternative declares success; Response carries transport errors or
//! additional statuses documented by Output annotations.
template < typename T, std::uint16_t Status = 200 >
using JsonResult = std::variant< JsonResponse< T, Status >, Response >;

} // namespace idhan::http
