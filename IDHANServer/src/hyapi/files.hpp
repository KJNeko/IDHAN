//
// Created by kj16609 on 11/2/24.
//
#pragma once
#include "api/helpers/ResponseCallback.hpp"
#include "drogon/utils/coroutine.h"

namespace idhan::hyapi
{
	void searchFiles( const drogon::HttpRequestPtr& request, ResponseFunction&& callback );

}