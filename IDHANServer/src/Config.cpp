#include "Config.hpp"

#include <ranges>

#include "logging/log.hpp"

namespace idhan::config
{

inline static std::string user_config_path { "" };
inline static std::unordered_map< std::pair< std::string, std::string >, std::string > CLI_CONFIG {};

void addCLIConfig( const std::string_view group, const std::string_view name, const std::string_view value )
{
	// The parser lets a repeated option win, so registering one twice has to overwrite.
	CLI_CONFIG.insert_or_assign( std::pair< std::string, std::string >( group, name ), std::string { value } );
}

const char* getCLIConfig( const std::string_view group, const std::string_view name )
{
	const auto itter { CLI_CONFIG.find( std::pair< std::string, std::string >( group, name ) ) };

	if ( itter == CLI_CONFIG.end() ) return nullptr;

	return itter->second.c_str();
}

std::string_view getUserConfigPath()
{
	return user_config_path;
}

void setLocation( std::filesystem::path path )
{
	user_config_path = path;
}

std::filesystem::path getLogPath()
{
	const std::string log_directory { config::getSilentDefault< std::string >( "logging", "path", "./log" ) };

	if ( !std::filesystem::exists( log_directory ) )
	{
		std::filesystem::create_directories( log_directory );
	}

	return log_directory;
}

} // namespace idhan::config
