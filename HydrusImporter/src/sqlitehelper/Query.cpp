//
// Created by kj16609 on 7/15/25.
//

#include "Query.hpp"

namespace idhan::hydrus
{

std::unique_ptr< sqlite3_stmt, StmtDeleter > prepareStatement( TransactionBaseCoro tr, std::string_view sql )
{
	sqlite3_stmt* stmt { nullptr };
	const char* unused_query { nullptr };
	const auto prepare_ret {
		sqlite3_prepare_v2( tr.db(), sql.data(), static_cast< int >( sql.size() + 1 ), &stmt, &unused_query )
	};

	if ( unused_query != nullptr && strlen( unused_query ) > 0 )
	{
		const std::string_view leftovers { unused_query };
		auto itter { leftovers.begin() };
		while ( itter != leftovers.end() )
		{
			if ( *itter == '\n' || *itter == '\t' )
			{
				++itter;
				continue;
			}

			throw std::
				runtime_error( std::format( "Query had unused portions of the input. Unused: \"{}\"", unused_query ) );
		}
	}

	if ( prepare_ret != SQLITE_OK )
	{
		throw std::runtime_error(
			std::format( "DB: Failed to prepare statement: \"{}\", Reason: \"{}\"", sql, sqlite3_errmsg( tr.db() ) ) );
	}

	if ( stmt == nullptr )
		throw std::runtime_error( std::format( "Failed to prepare stmt, {}", sqlite3_errmsg( tr.db() ) ) );

	return std::unique_ptr< sqlite3_stmt, StmtDeleter >( stmt );
}
} // namespace idhan::hydrus