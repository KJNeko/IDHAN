//
// Created by kj16609 on 11/8/24.
//

#include "IDHANAuth.hpp"

namespace idhan::api
{

	void IDHANAuth::invoke(
		const drogon::HttpRequestPtr& req, drogon::MiddlewareNextCallback&& nextCb, drogon::MiddlewareCallback&& mcb )
	{
		// continue
		nextCb( std::move( mcb ) );
	}
} // namespace idhan::api