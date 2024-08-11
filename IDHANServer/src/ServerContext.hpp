//
// Created by kj16609 on 7/23/24.
//

#pragma once
#include <memory>

namespace idhan
{
	struct ConnectionArguments;
	class Database;

	class ServerContext
	{
		std::unique_ptr< Database > m_db;

	  public:

		ServerContext() = delete;

		void setupCORSSupport();
		ServerContext( const ConnectionArguments& arguments );
		void run();

		~ServerContext();
	};

} // namespace idhan
