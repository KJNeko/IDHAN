//
// Created by kj16609 on 8/16/22.
//

#ifndef IDHAN_IDHANSQLITE_HPP
#define IDHAN_IDHANSQLITE_HPP

#include "objects/abstract/IDHANAbstractDatabase.hpp"


namespace IDHAN::Database
{
struct SqliteConnectionArgs
{
};

class IDHANSqlite : public AbstractDatabase
{
	// TODO:Define this

  public:
	using ConnectionArgs = SqliteConnectionArgs;


	IDHANSqlite( ConnectionArgs conn_args )
};
}
#endif	// IDHAN_IDHANSQLITE_HPP
