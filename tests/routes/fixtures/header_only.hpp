#pragma once

#include "idhan/http/routing/ReflectedRoutes.hpp"

namespace idhan::httpapi::automatic_test
{

struct Payload
{
	std::string name;
};

// clang-format off
[[= http::Get{"/automatic/header"}]]
[[= http::Get{"/automatic/alias"}]]
[[= http::Public{}]]
	// clang-format on
	inline http::HTTPTask< http::JsonResponse< Payload > >
		headerEndpoint()
{
	co_return http::JsonResponse< Payload > { { "discovered" } };
}

} // namespace idhan::httpapi::automatic_test
