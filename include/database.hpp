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


#include <mutex>


class Database
{
	inline static pqxx::connection* conn { nullptr };
	inline static std::recursive_mutex txn_mtx;
	std::shared_ptr< pqxx::work > txn;

	std::lock_guard< std::recursive_mutex > lk { txn_mtx };

	std::shared_ptr< bool > finalized { new bool( false ) };

public:
	Database() = default;

	Database( Database& db );

	static void initalizeConnection( const std::string& );

	std::shared_ptr< pqxx::work > getWorkPtr();

	void commit( bool throw_on_error = true );

	void abort( bool throw_on_error = true );

	~Database();
};

#endif // MAIN_DATABASE_HPP
