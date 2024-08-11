//
// Created by kj16609 on 7/24/24.
//

#pragma once

#include <pqxx/pqxx>

#include "../ConnectionArguments.hpp"

namespace idhan
{

	class Database
	{
		pqxx::connection connection;

		void initalSetup( pqxx::nontransaction& nontransaction );

		void importHydrus( const ConnectionArguments& connection_arguments );

	  public:

		Database( const ConnectionArguments& arguments );
	};

} // namespace idhan
