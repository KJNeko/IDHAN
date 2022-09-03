//
// Created by kj16609 on 9/2/22.
//

#include <idhan/database/database.hpp>

#include <catch2/catch_all.hpp>

#include <spdlog/spdlog.h>

using namespace idhan::database;

TEST_CASE("RecursiveConnectionTest", "[database]")
{
	spdlog::set_level(spdlog::level::debug);

	ConnectionManager manager("host=localhost dbname=template1 user=postgres password=postgres");

	RecursiveConnection rec_conn(manager);
	const auto work_ptr {rec_conn.getWork()};


	{
		RecursiveConnection rec_conn2(manager);
		const auto work_ptr2 {rec_conn2.getWork()};

		REQUIRE(work_ptr == work_ptr2);
	}
}

TEST_CASE("UniqueConnectionTest", "[database]")
{
	spdlog::set_level(spdlog::level::debug);

	ConnectionManager manager("host=localhost dbname=template1 user=postgres password=postgres");

	UniqueConnection conn1 {manager.acquire()};
	UniqueConnection conn2 {manager.acquire()};

	const auto& pool = internal::ConnectionManagerTester::pool(manager);

	const auto& connections = internal::ConnectionPoolTester::connections(pool);

	REQUIRE(connections.size() == 32 - 2);

	REQUIRE(&conn1.transaction != &conn2.transaction);
}

