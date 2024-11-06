//
// Created by kj16609 on 11/6/24.
//
#pragma once
#include "drogon/HttpRequest.h"
#include "helpers/ResponseCallback.hpp"

namespace idhan::api
{

	//! /
	void getIndex( const drogon::HttpRequestPtr& request, ResponseFunction&& callback );

	//! GET /version
	void getVersion( const drogon::HttpRequestPtr& request, ResponseFunction&& callback );

	inline void registerApi()
	{
		auto& app { drogon::app() };

		// free-access API.
		// app.registerHandler( "/", &getIndex );
		app.registerHandler( "/version", &getVersion );

		//Anything past this point should be blocked unless an access key is given
	}

} // namespace idhan::api
