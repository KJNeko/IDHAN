//
// Created by kj16609 on 9/8/24.
//

#pragma once

namespace pqxx
{
	class nontransaction;
}

namespace idhan::db
{
	void prepareInitalTables( pqxx::nontransaction& );
}
