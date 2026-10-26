#pragma once

#include <string_view>

#include "idhan/db/SearchPath.hpp"

namespace pqxx
{
class dbtransaction;
}

namespace idhan::db
{

// Create missing extensions in the configured namespace and initialize the application schema.
// Failure rolls back bootstrap changes; existing objects and extensions are left alone.
void initializeDatabase(
	pqxx::dbtransaction& tx,
	std::string_view schema,
	std::string_view extension_schema = EXTENSION_SCHEMA );

} // namespace idhan::db
