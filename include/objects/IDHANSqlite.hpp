//
// Created by kj16609 on 8/16/22.
//

#ifndef IDHAN_IDHANSQLITE_HPP
#define IDHAN_IDHANSQLITE_HPP

#include "abstract/IDHANAbstractDatabase.hpp"



struct SqliteConnectionArgs
{

};

class IDHANSqlite : public IDHANAbstractDatabase
{
	//TODO:Define this

  public:

	using ConnectionArgs = SqliteConnectionArgs;


	IDHANSqlite(ConnectionArgs conn_args)

};

#endif	// IDHAN_IDHANSQLITE_HPP
