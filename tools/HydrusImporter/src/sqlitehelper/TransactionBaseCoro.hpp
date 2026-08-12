#pragma once

#include <fgl/defines.hpp>
#include <sqlite3.h>

namespace idhan::hydrus
{

//! Minimal non-owning handle to a sqlite3 connection, passed by value into the Query/coroutine helpers.
class TransactionBaseCoro
{
	sqlite3* m_db;

  public:

	//! \return The underlying sqlite3 connection (asserts it is non-null).
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