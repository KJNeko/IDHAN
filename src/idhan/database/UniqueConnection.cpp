//
// Created by kj16609 on 9/3/22.
//

#include <idhan/database/UniqueConnection.hpp>
#include <spdlog/spdlog.h>

namespace idhan::database
{

UniqueConnection::UniqueConnection( ConnectionPool& conn_pool ) :
	pool( conn_pool ),
	connection( pool.acquire() ),
	transaction( *connection )
{
}

UniqueConnection::~UniqueConnection()
{
	pool.release( connection.release() );
}

}


