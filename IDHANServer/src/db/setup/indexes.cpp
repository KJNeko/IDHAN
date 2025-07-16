//
// Created by kj16609 on 9/13/24.
//

#include "indexes.hpp"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Weffc++"
#pragma GCC diagnostic ignored "-Wuseless-cast"
#pragma GCC diagnostic ignored "-Wswitch-enum"
#include <pqxx/nontransaction>
#pragma GCC diagnostic pop

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