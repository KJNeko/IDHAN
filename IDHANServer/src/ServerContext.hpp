//
// Created by kj16609 on 7/23/24.
//

#pragma once
#include <filesystem>
#include <memory>

namespace idhan
{
struct ConnectionArguments;
class ManagementConnection;

class ServerContext
{
	//! Connection to postgresql to be used for management/setup
	std::unique_ptr< ManagementConnection > m_postgresql_management;

  public:

	ServerContext() = delete;

	void setupCORSSupport();
	ServerContext( const ConnectionArguments& arguments );
	void run();

	~ServerContext();
};

} // namespace idhan
