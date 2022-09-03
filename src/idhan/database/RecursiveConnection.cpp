//
// Created by kj16609 on 9/3/22.
//

#include <idhan/database/RecursiveConnection.hpp>

namespace idhan::database
{

pqxx::work* RecursiveConnection::getWork() const
{
	return &( connection->transaction );
}

RecursiveConnection::RecursiveConnection( ConnectionManager& conn_manager ) :
	manager( conn_manager ),
	connection( manager.acquireShared() )
{
}

RecursiveConnection::~RecursiveConnection()
{
	manager.release();
}

}