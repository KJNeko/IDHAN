//
// Created by kj16609 on 8/13/22.
//

#pragma once
#ifndef IDHAN_APIBASE_HPP
#define IDHAN_APIBASE_HPP

#include <drogon/drogon.h>
#include <spdlog/spdlog.h>

#include <cstdint>
#include <filesystem>

#include "apifiles.hpp"

namespace api
{
class api_info : public drogon::HttpController< api_info >
{
  public:
	METHOD_LIST_BEGIN

	METHOD_ADD( api_info::get_v1, "/api_versions/v1", Get );
	METHOD_ADD( api_info::get_versions, "/api_version", Get );
	METHOD_ADD( api_info::api, "/", Get );

	METHOD_LIST_END

	using CallBack_Type = std::function< void( const HttpResponsePtr& ) >;

	void api( const HttpRequestPtr& req, CallBack_Type&& callback );

	void get_v1( const HttpRequestPtr& req, CallBack_Type&& callback );

	void get_versions( const HttpRequestPtr& req, CallBack_Type&& callback );
};
}  // namespace api

class IDHANWebAPI
{
  public:
	IDHANWebAPI( uint16_t port, uint16_t thread_count )
	{
		spdlog::info( "Starting drogon" );

		const std::filesystem::path path{ "./db/logs/" };

		std::filesystem::create_directories( path );

		drogon::app()
		  .setLogPath( path )
		  .setLogLevel( trantor::Logger::kWarn )
		  .addListener( "0.0.0.0", port )
		  .setThreadNum( thread_count )
		  .run();
	}
};

#endif	// IDHAN_APIBASE_HPP
