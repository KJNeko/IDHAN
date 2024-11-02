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

	bool tableExists( pqxx::nontransaction& tx, std::string_view name );

	//! Returns the table version.
	std::uint16_t getTableVersion( pqxx::nontransaction& tx, std::string_view name );

	void addTableToInfo( pqxx::nontransaction& tx, std::string_view name, std::string_view creation_query );

	void updateTableVersion( pqxx::nontransaction& tx, std::string_view name, std::uint16_t version );

#ifdef ALLOW_TABLE_DESTRUCTION
	void destroyTables( pqxx::nontransaction& );
#else
	inline void destroyTables( [[maybe_unused]] pqxx::nontransaction& )
	{
		return;
	}
#endif

} // namespace idhan::db