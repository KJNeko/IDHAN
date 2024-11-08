//
// Created by kj16609 on 9/13/24.
//

#include "indexes.hpp"

#include <pqxx/nontransaction>

#include <array>
#include <string_view>

namespace idhan::db
{

constexpr std::array< std::string_view, 0 > index_sql {

};

void prepareInitalIndexes( pqxx::nontransaction& tx )
{
	for ( const auto& sql : index_sql )
	{
		tx.exec( sql );
	}
}
} // namespace idhan::db