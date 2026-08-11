//
// Created by kj16609 on 8/10/26.
//
#pragma once

#include <string>
#include <string_view>

namespace pqxx
{
class connection;
}

//! The schema every test runs against. Tests force this unconditionally -- there is no mode flag
//! that selects it, and the server has no say in the matter.
constexpr std::string_view TEST_SCHEMA { "test" };

//! \return A libpq connection string for the test database, with search_path already set for
//!         TEST_SCHEMA via idhan::db::makeSearchPath.
[[nodiscard]] std::string testConnectionString();

//! Drops and recreates TEST_SCHEMA, then runs the migrations into it.
/** The destructive reset lives here rather than in the server. A clean slate is a property the test
 *  suite wants; a server binary should have no code path that drops a schema at all. */
void resetTestSchema( pqxx::connection& conn );
