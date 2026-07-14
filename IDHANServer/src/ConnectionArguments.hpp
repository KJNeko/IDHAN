//
// Created by kj16609 on 7/24/24.
//

#pragma once

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Weffc++"
#include <spdlog/spdlog.h>
#pragma GCC diagnostic pop

#include <string>

#include "Config.hpp"

//! Default PostgreSQL port used when none is configured.
#ifndef IDHAN_DEFAULT_POSTRES_PORT
constexpr std::uint16_t IDHAN_DEFAULT_POSTGRES_PORT { 5432 };
#endif

namespace idhan
{

//! Resolved server startup configuration: PostgreSQL connection settings plus logging and test-mode
//! flags. Each field defaults from the config system (config::get), overridable per the config
//! priority order (CLI > env > config files).
struct ConnectionArguments
{
	std::string hostname { config::get< std::string >( "database", "host", "localhost" ) };
	std::uint16_t port { config::get< std::uint16_t >( "database", "port", IDHAN_DEFAULT_POSTGRES_PORT ) };
	std::string dbname { config::get< std::string >( "database", "database", "idhan-db" ) };
	std::string user { config::get< std::string >( "database", "user", "idhan" ) };
	std::string password { config::get< std::string >( "database", "password", "idhan" ) };
	bool testmode { false };
	//! If true then the server will use stdout to log things.
	bool use_stdout { true };
	spdlog::level::level_enum log_level { spdlog::level::info };

	//! \return A libpq connection string built from these arguments.
	std::string format() const;
};

} // namespace idhan
