//
// Created by kj16609 on 9/4/22.
//

#pragma once
#ifndef IDHAN_DATABASE_HPP
#define IDHAN_DATABASE_HPP




class Database
{

	//! Pool of pqxx::connection
	inline static ConnectionPool conn_pool;

	//! Manager for all connections
	inline static ConnectionManager conn_manager;




};


class ThreadManager
{



	static ThreadData thing()
};


class Thread
{




};

#endif	// IDHAN_DATABASE_HPP
