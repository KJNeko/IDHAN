#pragma once

#include "idhan/http/SSEEventHub.hpp"

namespace idhan::server
{

[[nodiscard]] inline http::SSEEventHub& events()
{
	// Jobs and downloader services can outlive ServerContext during static teardown.
	static auto* hub { new http::SSEEventHub {} };
	return *hub;
}

} // namespace idhan::server
