//
// Created by kj16609 on 9/3/22.
//


#pragma once
#ifndef IDHAN_RECURSIVECONNECTION_HPP
#define IDHAN_RECURSIVECONNECTION_HPP

#include <idhan/database/ConnectionManager.hpp>

namespace idhan::database
{

class RecursiveConnection
{
	ConnectionManager& manager;
	std::shared_ptr<UniqueConnection> connection;


public:

  RecursiveConnection(ConnectionManager& conn_manager);

  [[nodiscard]] pqxx::work* getWork() const;

  ~RecursiveConnection();
};

typedef RecursiveConnection connection;
}
#endif	// IDHAN_RECURSIVECONNECTION_HPP
