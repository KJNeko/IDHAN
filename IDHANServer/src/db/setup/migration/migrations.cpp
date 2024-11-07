//
// Created by kj16609 on 11/7/24.
//

#include <pqxx/pqxx>

#include <cstdint>

namespace idhan::db
{

	void updateMigrations( pqxx::nontransaction& tx )
	{
		std::size_t current_id { 0 };

		// attempt to get the most recent update id
	}

} // namespace idhan::db
