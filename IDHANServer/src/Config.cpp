//
// Created by kj16609 on 11/8/24.
//
#include "Config.hpp"

#include <ranges>

#include "logging/log.hpp"

namespace idhan::config
{

static std::string user_config_path { "" };

std::string_view getUserConfigPath()
{
	return user_config_path;
}

void setLocation( std::filesystem::path path )
{
	user_config_path = path;
}

} // namespace idhan::config
