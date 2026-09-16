#include "idhan/db/DBConfig.hpp"

#include <stdexcept>

namespace idhan::db
{
void DBConfig::validate() const
{
	if ( host.empty() || database.empty() || user.empty() || schema.empty() )
		throw std::invalid_argument( "PostgreSQL host, database, user, and schema must not be empty" );

	if ( port == 0 ) throw std::invalid_argument( "PostgreSQL port must be nonzero" );

	if ( connections == 0 || transaction_connections == 0 )
		throw std::invalid_argument( "PostgreSQL normal and transaction pools require at least one connection" );

	if ( diagnostic_value_bytes == 0 ) throw std::invalid_argument( "PostgreSQL diagnostic limit must be nonzero" );
}

DBConfig DBConfig::defaults()
{
	return {};
}
} // namespace idhan::db
