//
// Created by kj16609 on 11/8/24.
//
#include "Config.hpp"

#include <atomic>
#include <fstream>

namespace idhan::config
{

inline static constexpr std::string_view config_path { "./config.toml" };
static std::mutex config_mtx;
static toml::parse_result config;
//! If true then the config should not be retrieved from disk again
static std::atomic< bool > config_updated;

toml::parse_result loadConfig()
{
	std::lock_guard guard { config_mtx };
	if ( !config_updated )
	{
		config = toml::parse_file( config_path );
	}

	return config;
}

void saveConfig( const toml::parse_result& modified_config )
{
	std::lock_guard guard { config_mtx };
	if ( std::ofstream ofs( std::filesystem::path( config_path ), std::ofstream::trunc ); ofs )
	{
		toml::toml_formatter formatter { modified_config };

		ofs << formatter;
	}

	config = std::move( modified_config );
}

} // namespace idhan::config
