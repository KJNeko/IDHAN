//
// Created by kj16609 on 6/1/22.
//

#pragma once
#ifndef MAIN_DATABASE_HPP
#define MAIN_DATABASE_HPP

// Don't make me box you
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wrestrict"
#pragma GCC diagnostic ignored "-Wuseless-cast"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#pragma GCC diagnostic ignored "-Wmultiple-inheritance"
#pragma GCC diagnostic ignored "-Wswitch-default"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wpadded"
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

#include <pqxx/pqxx>

#pragma GCC diagnostic pop

#include <memory>

class Database
{
	inline static pqxx::connection* conn { nullptr };
	inline static std::mutex mtx;
	std::shared_ptr<pqxx::work> txn;
	std::shared_ptr<std::lock_guard<std::mutex>> guard;

	inline static std::mutex conMtx;

  public:
	Database();
	Database( Database& );

	static void initalizeConnection( const std::string& );

	pqxx::work& getWork();
};


#endif // MAIN_DATABASE_HPP
