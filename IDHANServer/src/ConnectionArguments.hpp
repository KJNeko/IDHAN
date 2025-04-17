//
// Created by kj16609 on 7/24/24.
//

#pragma once

#include <filesystem>
#include <string>

#ifndef IDHAN_DEFAULT_POSTRES_PORT
constexpr std::uint16_t IDHAN_DEFAULT_POSTGRES_PORT { 5432 };
#endif

namespace idhan
{

struct ConnectionArguments
{
	std::string hostname { "" };
	std::uint16_t port { IDHAN_DEFAULT_POSTGRES_PORT };
	std::string dbname { "idhan-db" };
	std::string user { "idhan" };
	std::string password { "" };
	bool testmode { false };
	//! If true then the server will use stdout to log things.
	bool use_stdout { false };

	std::string format() const;
};

} // namespace idhan