//
// Created by kj16609 on 9/13/24.
//

#pragma once

namespace pqxx
{
class nontransaction;
}

namespace idhan::db
{
void prepareSQLFunctions( pqxx::nontransaction& tx );
}
