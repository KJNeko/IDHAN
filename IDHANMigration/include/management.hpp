//
// Created by kj16609 on 9/8/24.
//

#pragma once
#include <cstddef>
#include <string_view>

namespace pqxx
{
class nontransaction;
}

namespace idhan::db
{

[[nodiscard]] bool tableExists( pqxx::nontransaction& tx, std::string_view name, std::string_view schema );

void addTableToInfo(
	pqxx::nontransaction& tx,
	std::string_view name,
	std::string_view operation,
	std::string_view creation_query,
	std::size_t migration_id );

} // namespace idhan::db