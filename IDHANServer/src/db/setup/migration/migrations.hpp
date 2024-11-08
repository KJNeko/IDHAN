//
// Created by kj16609 on 11/7/24.
//
#pragma once
#include <pqxx/nontransaction>

#include <cstddef>

namespace idhan::db
{
	std::size_t doMigration( pqxx::nontransaction& tx, std::size_t migration_id );

	void updateMigrations( pqxx::nontransaction& tx );

} // namespace idhan::db
