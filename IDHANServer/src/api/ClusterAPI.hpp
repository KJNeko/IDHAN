//
// Created by kj16609 on 11/15/24.
//
#pragma once
#include <expected>

#include "IDHANTypes.hpp"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Weffc++"
#pragma GCC diagnostic ignored "-Wredundant-tags"
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wnoexcept"
#pragma GCC diagnostic ignored "-Wredundant-decls"
#pragma GCC diagnostic ignored "-Wuseless-cast"
#pragma GCC diagnostic ignored "-Wnoexcept"
#pragma GCC diagnostic ignored "-Wswitch-enum"
#pragma GCC diagnostic ignored "-Wshadow"
#include "drogon/HttpController.h"
#include "drogon/utils/coroutine.h"
#pragma GCC diagnostic pop

namespace idhan::api
{

struct ClusterInfo
{
	ClusterID m_id;

	std::string cluster_name;

	std::filesystem::path path;

	std::uint16_t ratio;

	struct
	{
		std::size_t used;
		std::size_t total;
		std::size_t limit;
	} size;

	std::uint32_t file_count;
	bool read_only;
	bool allowed_thumbnails;
	bool allowed_files;

	void addSize( std::size_t size );
	void removeSize( std::size_t size );

  public:

	std::filesystem::path getPath( RecordID id ) const;
	void storeFile( const std::vector< std::byte >& data );
};

drogon::Task< std::expected< Json::Value, drogon::HttpResponsePtr > >
	getInfo( ClusterID cluster_id, drogon::orm::DbClientPtr transaction );

class ClusterAPI : public drogon::HttpController< ClusterAPI >
{
	using ResponseTask = drogon::Task< drogon::HttpResponsePtr >;

	ResponseTask modifyT( drogon::HttpRequestPtr request, ClusterID cluster_id, drogon::orm::DbClientPtr transaction );

	ResponseTask infoT( drogon::HttpRequestPtr request, ClusterID cluster_id, drogon::orm::DbClientPtr transaction );

	ResponseTask add( drogon::HttpRequestPtr request );
	ResponseTask list( drogon::HttpRequestPtr request );
	ResponseTask info( drogon::HttpRequestPtr request, ClusterID cluster_id );
	ResponseTask modify( drogon::HttpRequestPtr request, ClusterID cluster_id );
	ResponseTask remove( drogon::HttpRequestPtr request, ClusterID cluster_id );

	ResponseTask scan( drogon::HttpRequestPtr request, ClusterID cluster_id );

  public:

	METHOD_LIST_BEGIN

	ADD_METHOD_TO( ClusterAPI::add, "/clusters/add" );
	ADD_METHOD_TO( ClusterAPI::list, "/clusters/list" );
	ADD_METHOD_TO( ClusterAPI::info, "/clusters/{cluster_id}/info" );
	ADD_METHOD_TO( ClusterAPI::modify, "/clusters/{cluster_id}/modify" );
	ADD_METHOD_TO( ClusterAPI::remove, "/clusters/{cluster_id}/remove" );
	ADD_METHOD_TO( ClusterAPI::scan, "/clusters/{cluster_id}/scan" );

	METHOD_LIST_END
};

} // namespace idhan::api