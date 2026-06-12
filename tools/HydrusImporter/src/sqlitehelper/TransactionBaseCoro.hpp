//
// Created by kj16609 on 7/12/25.
//
#pragma once

#include <fgl/defines.hpp>
#include <sqlite3.h>

namespace idhan::hydrus
{

class TransactionBaseCoro
{
	sqlite3* m_db;

  public:

	sqlite3* db()
	{
		FGL_ASSERT( m_db != nullptr, "Database pointer was null in TransactionBaseCoro" );
		return m_db;
	}

	TransactionBaseCoro( sqlite3* db ) : m_db( db )
	{
		FGL_ASSERT( db != nullptr, "Database pointer was null in TransactionBaseCoro constructor" );
	}
};

} // namespace idhan::hydrus