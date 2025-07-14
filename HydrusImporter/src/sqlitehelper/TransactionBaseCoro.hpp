//
// Created by kj16609 on 7/12/25.
//
#pragma once

#include <sqlite3.h>

namespace idhan::hydrus
{

class TransactionBaseCoro
{
	sqlite3* m_db;

  public:

	sqlite3* db() { return m_db; }

	TransactionBaseCoro( sqlite3* db ) : m_db( db ) {}
};

} // namespace idhan::hydrus