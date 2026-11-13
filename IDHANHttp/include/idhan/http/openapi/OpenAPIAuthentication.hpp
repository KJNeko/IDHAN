#pragma once

#include <span>
#include <string_view>

namespace idhan::http
{

struct OpenAPIAuthentication
{
	std::span< const std::string_view > headers {};
	std::string_view query {};
};

} // namespace idhan::http
