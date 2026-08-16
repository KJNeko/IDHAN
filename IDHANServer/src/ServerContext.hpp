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

namespace idhan
{
class ManagementConnection;

void addCORSHeaders( const drogon::HttpResponsePtr& response );

class ServerContext
{
	std::shared_ptr< spdlog::logger > m_logger;
	std::unique_ptr< ManagementConnection > m_postgresql_management;
	ConnectionArguments args;
	std::unique_ptr< filesystem::ClusterManager > m_clusters {};
	std::unique_ptr< modules::ModuleLoader > m_module_loader {};

  public:

	ServerContext() = delete;

	ServerContext( const ConnectionArguments& arguments );

	void setupCORSSupport() const;

	//! Serves the WebUI's index.html for unmatched non-API navigations so client-side routes survive a reload.
	void setupSPAFallback() const;
	static std::shared_ptr< spdlog::logger > createLogger( const ConnectionArguments& arguments );
	void run();

	~ServerContext();
};

} // namespace idhan
