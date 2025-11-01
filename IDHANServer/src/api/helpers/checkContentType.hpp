//
// Created by kj16609 on 11/2/24.
//
#pragma once
#include <vector>

#include "ResponseCallback.hpp"

namespace idhan
{
//! Responds with that the content type is unsupported or unknown
void checkContentType(
	const drogon::HttpRequestPtr& request,
	const ResponseFunction& callback,
	const std::vector< drogon::ContentType > expected );

} // namespace idhan
