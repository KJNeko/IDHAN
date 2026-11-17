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

class ManagementConnection
{
	pqxx::connection connection;

  public:

	inline pqxx::connection& conn() { return connection; }

	ManagementConnection( const ConnectionArguments& arguments );
};

} // namespace idhan
