//
// Created by kj16609 on 9/3/22.
//


#pragma once
#ifndef IDHAN_UNIQUECONNECTION_HPP
#define IDHAN_UNIQUECONNECTION_HPP

#include <memory>
#include <pqxx/pqxx>

#include <idhan/database/ConnectionPool.hpp>

namespace idhan::database
{

class UniqueConnection
{
	idhan::database::ConnectionPool& pool;
	std::unique_ptr< pqxx::connection > connection;


  public:
	pqxx::work transaction;

	UniqueConnection( idhan::database::ConnectionPool& conn_pool );

	~UniqueConnection();
};

}
#endif	// IDHAN_UNIQUECONNECTION_HPP
