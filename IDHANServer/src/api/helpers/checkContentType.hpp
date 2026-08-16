#pragma once
#include <vector>

#include "ResponseCallback.hpp"

namespace idhan::api::helpers
{
//! Responds with that the content type is unsupported or unknown
void checkContentType(
	const drogon::HttpRequestPtr& request,
	const ResponseFunction& callback,
	const std::vector< drogon::ContentType >& expected );

} // namespace idhan::api::helpers
