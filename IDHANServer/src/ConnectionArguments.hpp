#pragma once

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Weffc++"
#include <spdlog/spdlog.h>
#pragma GCC diagnostic pop

#include <string>

#include "Config.hpp"
#include "db/searchPath.hpp"

#ifndef IDHAN_DEFAULT_POSTRES_PORT
constexpr std::uint16_t IDHAN_DEFAULT_POSTGRES_PORT { 5432 };
#endif

namespace idhan
{

struct ConnectionArguments
{
	std::string hostname { config::get< std::string >( "database", "host", "localhost" ) };
	std::uint16_t port { config::get< std::uint16_t >( "database", "port", IDHAN_DEFAULT_POSTGRES_PORT ) };
	std::string dbname { config::get< std::string >( "database", "database", "idhan-db" ) };
	std::string user { config::get< std::string >( "database", "user", "idhan" ) };
	std::string password { config::get< std::string >( "database", "password", "idhan" ) };
	std::string schema { config::get< std::string >( "database", "schema", std::string { db::DEFAULT_SCHEMA } ) };
	bool use_stdout { true };
	spdlog::level::level_enum log_level { spdlog::level::info };

	[[nodiscard]] std::string searchPath() const { return db::makeSearchPath( schema ); }

	std::string format() const;
};

} // namespace idhan
