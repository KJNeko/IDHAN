#pragma once

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Weffc++"
#include <spdlog/spdlog.h>
#pragma GCC diagnostic pop

#include <filesystem>
#include <memory>
#include <thread>

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

	std::unique_ptr< httpapi::HeartbeatService > m_heartbeat {};

	std::unique_ptr< http::HTTPServer > m_http_server {};

	std::jthread m_module_maintenance_thread;

	void startReplacementStack();

  public:

	ServerContext() = delete;

	ServerContext( const ConnectionArguments& arguments );

	static std::shared_ptr< spdlog::logger > createLogger( const ConnectionArguments& arguments );
	void run();

	~ServerContext();
};

} // namespace idhan
