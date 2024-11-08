//
// Created by kj16609 on 11/8/24.
//
#pragma once
#include "drogon/HttpController.h"
#include "helpers/ResponseCallback.hpp"

namespace idhan::api
{

	class IDHANApi : public drogon::HttpController< IDHANApi >
	{
		void version( const drogon::HttpRequestPtr& request, ResponseFunction&& callback );

	  public:

		METHOD_LIST_BEGIN

		ADD_METHOD_TO( IDHANApi::version, "/version" );

		METHOD_LIST_END
	};

} // namespace idhan::api
