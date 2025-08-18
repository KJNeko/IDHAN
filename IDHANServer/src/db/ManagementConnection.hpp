//
// Created by kj16609 on 7/24/24.
//

#pragma once

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Weffc++"
#pragma GCC diagnostic ignored "-Wuseless-cast"
#pragma GCC diagnostic ignored "-Wswitch-enum"
#include <pqxx/pqxx>
#pragma GCC diagnostic pop

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
