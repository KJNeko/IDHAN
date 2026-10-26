#pragma once

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Weffc++"
#include <spdlog/spdlog.h>
#pragma GCC diagnostic pop

#include <filesystem>
#include <memory>

#include "ConnectionArguments.hpp"
#include "filesystem/clusters/ClusterManager.hpp"
#include "modules/ModuleLoader.hpp"

namespace folly
{
class CPUThreadPoolExecutor;
}

namespace idhan::db
{
class DBContext;
}

namespace idhan::maintenance
{
class HashMaintenance;
class ModuleMaintenance;
class TagCountMaintenance;
}

namespace idhan::http
{
class HTTPServer;
}

namespace idhan::httpapi
{
class HeartbeatService;
}

namespace idhan
{
class ManagementConnection;

class ServerContext
{
	std::shared_ptr< spdlog::logger > m_logger;
	std::unique_ptr< ManagementConnection > m_postgresql_management;
	ConnectionArguments args;
	std::unique_ptr< filesystem::ClusterManager > m_clusters {};
	std::unique_ptr< modules::ModuleLoader > m_module_loader {};

	std::shared_ptr< folly::CPUThreadPoolExecutor > m_db_executor {};
	std::unique_ptr< db::DBContext > m_db_context {};

	// Keep module health checks alive while hash workers drain, including failed startup.
	std::unique_ptr< maintenance::ModuleMaintenance > m_module_maintenance {};
	std::unique_ptr< maintenance::HashMaintenance > m_hash_maintenance {};
	std::unique_ptr< maintenance::TagCountMaintenance > m_tag_count_maintenance {};
	std::unique_ptr< httpapi::HeartbeatService > m_heartbeat {};

	std::unique_ptr< http::HTTPServer > m_http_server {};

	void startReplacementStack();

  public:

	ServerContext() = delete;

	ServerContext( const ConnectionArguments& arguments );

	static std::shared_ptr< spdlog::logger > createLogger( const ConnectionArguments& arguments );
	void run();

	~ServerContext();
};

} // namespace idhan
