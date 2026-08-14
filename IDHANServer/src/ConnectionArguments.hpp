#pragma once

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Weffc++"
#include <spdlog/spdlog.h>
#pragma GCC diagnostic pop

#include <string>

#include "Config.hpp"
#include "db/searchPath.hpp"

//! Default PostgreSQL port used when none is configured.
#ifndef IDHAN_DEFAULT_POSTRES_PORT
constexpr std::uint16_t IDHAN_DEFAULT_POSTGRES_PORT { 5432 };
#endif

namespace idhan
{

//! Resolved server startup configuration: PostgreSQL connection settings, the schema the server
//! occupies, and logging options. Each field defaults from the config system (config::get),
//! overridable per the config priority order (CLI > env > config files).
struct ConnectionArguments
{
	std::string hostname { config::get< std::string >( "database", "host", "localhost" ) };
	std::uint16_t port { config::get< std::uint16_t >( "database", "port", IDHAN_DEFAULT_POSTGRES_PORT ) };
	std::string dbname { config::get< std::string >( "database", "database", "idhan-db" ) };
	std::string user { config::get< std::string >( "database", "user", "idhan" ) };
	std::string password { config::get< std::string >( "database", "password", "idhan" ) };
	//! The PostgreSQL schema the server occupies. Tests force "test"; production leaves it default.
	std::string schema { config::get< std::string >( "database", "schema", std::string { db::DEFAULT_SCHEMA } ) };
	//! If true then the server will use stdout to log things.
	bool use_stdout { true };
	spdlog::level::level_enum log_level { spdlog::level::info };

	//! \return The search_path both the migration and the runtime connection must use. Derived, never
	//!         spelled out at a call site. See idhan::db::makeSearchPath.
	[[nodiscard]] std::string searchPath() const { return db::makeSearchPath( schema ); }

	//! \return A libpq connection string built from these arguments.
	std::string format() const;
};

} // namespace idhan
