//
// Created by kj16609 on 11/8/24.
//
#include "Config.hpp"

#include <ranges>

#include "logging/log.hpp"

namespace idhan::config
{

inline static std::string user_config_path { "" };
inline static std::unordered_map< std::pair< std::string, std::string >, std::string > CLI_CONFIG {};

void addCLIConfig( const std::string_view group, const std::string_view name, const std::string_view value )
{
	CLI_CONFIG.emplace( std::pair< std::string, std::string >( group, name ), value );
}

const char* getCLIConfig( const std::string_view group, const std::string_view name )
{
	return CLI_CONFIG.at( std::pair< std::string, std::string >( group, name ) ).c_str();
}

std::string_view getUserConfigPath()
{
	return user_config_path;
}

void setLocation( std::filesystem::path path )
{
	user_config_path = path;
}

} // namespace idhan::config
