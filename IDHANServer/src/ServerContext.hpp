//
// Created by kj16609 on 7/23/24.
//

#pragma once
#include <spdlog/spdlog.h>

#include <filesystem>
#include <memory>

#include "ConnectionArguments.hpp"
#include "filesystem/ClusterManager.hpp"
#include "modules/ModuleLoader.hpp"

namespace idhan
{
class ManagementConnection;

class ServerContext
{
	std::shared_ptr< spdlog::logger > m_logger;
	//! Connection to postgresql to be used for management/setup
	std::unique_ptr< ManagementConnection > m_postgresql_management;
	ConnectionArguments args;
	std::unique_ptr< filesystem::ClusterManager > m_clusters {};
	std::unique_ptr< modules::ModuleLoader > m_module_loader {};

  public:

	ServerContext() = delete;

	void setupCORSSupport() const;
	static std::shared_ptr< spdlog::logger > createLogger( const ConnectionArguments& arguments );
	ServerContext( const ConnectionArguments& arguments );
	void run();

	~ServerContext();
};

} // namespace idhan
