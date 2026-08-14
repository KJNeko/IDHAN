#pragma once

#include <string>
#include <string_view>

namespace pqxx
{
class connection;
}

//! The schema every test runs against.
constexpr std::string_view TEST_SCHEMA { "test" };

//! \return A libpq connection string for the test database, with search_path already set for
//!         TEST_SCHEMA via idhan::db::makeSearchPath.
[[nodiscard]] std::string testConnectionString();

//! Drops and recreates TEST_SCHEMA, then runs the migrations into it.
void resetTestSchema( pqxx::connection& conn );
