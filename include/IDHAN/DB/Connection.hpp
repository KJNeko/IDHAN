//
// Created by kj16609 on 5/11/22.
//

#ifndef IDHAN_CONNECTION_HPP
#define IDHAN_CONNECTION_HPP

#include <pqxx/pqxx>

#include <mutex>

#include "include/IDHAN/Utility/Config.hpp"

class Connection
{
public:
	static pqxx::connection conn;

private:

	static std::mutex lock;
	std::lock_guard<std::mutex> guard{lock};

public:



	Connection()
	{
		if(conn.is_open())
		{
			return;
		}
		else
		{
			conn = pqxx::connection(getDBString());
		}
	}

	static void resetConn()
	{
		conn = pqxx::connection(getDBString());
	}

	static pqxx::connection& getConn()
	{
		return conn;
	}
};







#endif //IDHAN_CONNECTION_HPP
