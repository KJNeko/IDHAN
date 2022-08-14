//
// Created by kj16609 on 8/13/22.
//

#pragma once
#ifndef IDHAN_APIFILES_HPP
#define IDHAN_APIFILES_HPP

#include <drogon/HttpController.h>
#include <spdlog/spdlog.h>

using namespace drogon;

namespace api
{
namespace v1
{

	class Files : public drogon::HttpController< Files >
	{
	  public:
		METHOD_LIST_BEGIN
		METHOD_ADD( Files::getInfo, "/{id}/info", Get );
		// api/v1/Files/{arg1}/info

		METHOD_ADD( Files::addTag, "/{id}/add_tag", Post );
		// api/v1/Files/{arg1}/add_tag

		METHOD_ADD( Files::removeTag, "/{id}/delete_tag", Delete );
		// api/v1/Files/{arg1}/delete_tag

		METHOD_ADD( Files::addFile, "/add_file", Post );
		// api/v1/Files/add_file

		METHOD_ADD( Files::deleteFile, "/{id}/delete", Delete );
		// api/v1/Files/{id}/delete

		METHOD_ADD( Files::undeleteFile, "/{id}/undelete", Post );
		// api/v1/Files/{id}/undelete

		METHOD_LIST_END

		using CallBack_Type = std::function< void( const HttpResponsePtr& ) >;

		void getInfo( const HttpRequestPtr& req, CallBack_Type&& callback, uint64_t id );

		void addFile( const HttpRequestPtr& req, CallBack_Type&& callback );

		void addTag( const HttpRequestPtr& req, CallBack_Type&& callback, uint64_t id );

		void removeTag( const HttpRequestPtr& req, CallBack_Type&& callback, uint64_t id );

		void deleteFile( const HttpRequestPtr& req, CallBack_Type&& callback, uint64_t id );

		void undeleteFile( const HttpRequestPtr& req, CallBack_Type&& callback, uint64_t id );

		Files() { spdlog::info( "Initalizing Files paths" ); }
	};

}  // namespace v1

}  // namespace api

#endif	// IDHAN_APIFILES_HPP
