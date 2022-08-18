//
// Created by kj16609 on 8/16/22.
//

#ifndef IDHAN_IDHANPOSTGRESQL_HPP
#define IDHAN_IDHANPOSTGRESQL_HPP

#include <pqxx/pqxx>

#include "IDHANDatabase.hpp"
#include "objects/abstract/IDHANAbstractDatabase.hpp"

namespace IDHAN::Database
{

//! Additional arguments for the postgresql connection
struct PostgresqlAdditionalArgs
{
	//! Connection arguments for <a href="https://libpqxx.readthedocs.io/en/latest/a00844.html">pqxx::connection</a> (See <a href="https://libpqxx.readthedocs.io/en/latest/index.html">libpqxx</a> docs)
	std::string connection_args;

	//! Defines the number of threads/connections to have active at once time
	uint8_t thread_count {1};

};

class IDHANPostgresql : public AbstractDatabase
{
	pqxx::connection conn;

  public:

	//! Simply here to allow for abstraction of this class even further
	using ConnectionArgs_T = PostgresqlAdditionalArgs;

	IDHANPostgresql(PostgresqlAdditionalArgs args) : conn(args.connection_args)
	{
		//Do more setup here if required
	}
};
}



#endif	// IDHAN_IDHANPOSTGRESQL_HPP
