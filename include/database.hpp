//
// Created by kj16609 on 6/1/22.
//

#ifndef MAIN_DATABASE_HPP
#define MAIN_DATABASE_HPP

// Don't make me box you
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wrestrict"
#pragma GCC diagnostic ignored "-Wuseless-cast"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#pragma GCC diagnostic ignored "-Wmultiple-inheritance"
#pragma GCC diagnostic ignored "-Wswitch-default"

#include <pqxx/pqxx>

#pragma GCC diagnostic pop


class Database
{
	inline static pqxx::connection conn { "dbname=idhan user=idhan password=idhan host=localhost" };
	inline static std::mutex mtx;
	std::lock_guard<std::mutex> lock_ptr { mtx };

  public:
	pqxx::work getWork();

	void release();

	void init();

	~Database();
};


#endif // MAIN_DATABASE_HPP
