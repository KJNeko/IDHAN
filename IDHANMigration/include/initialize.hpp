#pragma once

#include <string_view>

namespace pqxx
{
class transaction_base;
}

namespace idhan::db
{

// Check administrator-provisioned extensions and create the application schema in a
// transaction before migrations. Unrelated schemas and existing objects are left alone.
void initializeDatabase( pqxx::transaction_base& tx, std::string_view schema );

} // namespace idhan::db
