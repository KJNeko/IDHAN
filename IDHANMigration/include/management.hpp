//
// Created by kj16609 on 9/8/24.
//

#pragma once
#include <cstdint>
#include <string_view>

namespace pqxx
{
class nontransaction;
}

namespace idhan::db
{

bool tableExists( pqxx::nontransaction& tx, std::string_view name, std::string_view schema );

//! Returns the table version.
std::uint16_t getTableVersion( pqxx::nontransaction& tx, std::string_view name );

void addTableToInfo(
	pqxx::nontransaction& tx,
	std::string_view name,
	std::string_view creation_query,
	std::size_t migration_id );

} // namespace idhan::db