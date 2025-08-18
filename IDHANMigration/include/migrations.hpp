//
// Created by kj16609 on 11/7/24.
//
#pragma once

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Weffc++"
#pragma GCC diagnostic ignored "-Wuseless-cast"
#pragma GCC diagnostic ignored "-Wswitch-enum"
#include <pqxx/nontransaction>
#pragma GCC diagnostic pop

#include <cstddef>

namespace idhan::db
{
std::size_t doMigration( pqxx::nontransaction& tx, std::size_t migration_id );
void updateMigrations( pqxx::nontransaction& tx, std::string_view schema );

} // namespace idhan::db
