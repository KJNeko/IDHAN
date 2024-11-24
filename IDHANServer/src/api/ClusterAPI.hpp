//
// Created by kj16609 on 11/15/24.
//
#pragma once
#include "IDHANTypes.hpp"
#include "drogon/HttpController.h"
#include "drogon/HttpResponse.h"
#include "drogon/utils/coroutine.h"

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

class ClusterAPI : public drogon::HttpController< ClusterAPI >
{
	using ResponseTask = drogon::Task< drogon::HttpResponsePtr >;

	ResponseTask modifyT(
		drogon::HttpRequestPtr request, ClusterID cluster_id, std::shared_ptr< drogon::orm::Transaction > transaction );

	ResponseTask infoT(
		drogon::HttpRequestPtr request, ClusterID cluster_id, std::shared_ptr< drogon::orm::Transaction > transaction );

	ResponseTask add( drogon::HttpRequestPtr request );
	ResponseTask list( drogon::HttpRequestPtr request );
	ResponseTask info( drogon::HttpRequestPtr request, const ClusterID cluster_id );
	ResponseTask modify( drogon::HttpRequestPtr request, const ClusterID cluster_id );
	ResponseTask remove( drogon::HttpRequestPtr request, const ClusterID cluster_id );

  public:

	METHOD_LIST_BEGIN

	ADD_METHOD_TO( ClusterAPI::add, "/clusters/add" );
	ADD_METHOD_TO( ClusterAPI::list, "/clusters/list" );
	ADD_METHOD_TO( ClusterAPI::info, "/clusters/{cluster_id}/info" );
	ADD_METHOD_TO( ClusterAPI::modify, "/clusters/{cluster_id}/modify" );
	ADD_METHOD_TO( ClusterAPI::remove, "/clusters/{cluster_id}/remove" );

	METHOD_LIST_END
};

} // namespace idhan::api