//
// Created by kj16609 on 7/24/24.
//

#pragma once

#include <pqxx/pqxx>

namespace idhan
{
struct ConnectionArguments;

//! This class is used for the inital setup, migrations, and management of the idhan database.
class ManagementConnection
{
	pqxx::connection connection;

	void initalSetup( pqxx::nontransaction& nontransaction );

  public:

	inline pqxx::connection& conn() { return connection; }

	ManagementConnection( const ConnectionArguments& arguments );
};

} // namespace idhan
