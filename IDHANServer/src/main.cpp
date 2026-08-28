#include <array>
#include <cstdlib>
#include <malloc.h>

#include "CommandLine.hpp"
#include "ConnectionArguments.hpp"
#include "ServerContext.hpp"
#include "fgl/defines.hpp"
#include "logging/log.hpp"

using idhan::cli::Option;

static constexpr Option log_level {
	"log_level",
	"Dictates the log level used (trace, debug, info, warning, error, critical)",
	"level",
	"info"
};

static constexpr Option pg_user { "pg_user", "The user to connect to the database with", "pg_user", "idhan" };

static constexpr Option pg_host { "pg_host", "The host to connect to the database with", "pg_host", "localhost" };

static constexpr Option use_stdout { "use_stdout", "Use stdout for logging", "on/off", "true", "true" };

static constexpr Option pg_schema { "pg_schema", "The PostgreSQL schema to use", "pg_schema", "public" };

static constexpr Option
	config_location { "config", "The location for the config file", "config_location", "./config.toml" };

static constexpr Option
	force_start { "force_start", "Forces IDHAN to start even if it thinks it shouldn't", "on/off", "false", "true" };

static constexpr auto options {
	std::to_array< Option >( { log_level, pg_user, pg_host, use_stdout, pg_schema, config_location, force_start } )
};

void applyCLISettings(
	const std::string_view group,
	const std::string_view name,
	const idhan::cli::Parser& parser,
	const idhan::cli::Option& option )
{
	using namespace idhan::config;
	if ( parser.isSet( option ) )
	{
		addCLIConfig( group, name, parser.value( option ) );
	}
}

void checkForceStart( const idhan::cli::Parser& parser, const idhan::cli::Option& option )
{
	if ( parser.isSet( option ) && idhan::config::parseBool( parser.value( option ) ).value_or( false ) )
	{
		const std::filesystem::path tmp_path {
			idhan::config::getSilentDefault< std::string >( "server", "temp_path", "/tmp/idhan" )
		};
		constexpr std::string_view marker_file { "idhan.active" };
		std::filesystem::remove( tmp_path / marker_file );
	}
}

auto strToSpdlogLevel( const std::string& level )
{
	if ( level == "trace" ) return spdlog::level::trace;
	if ( level == "debug" ) return spdlog::level::debug;
	if ( level == "info" ) return spdlog::level::info;
	if ( level == "warning" || level == "warn" ) return spdlog::level::warn;
	if ( level == "error" ) return spdlog::level::err;
	if ( level == "critical" ) return spdlog::level::critical;
	spdlog::critical( "Invalid log level, Expected one of: (trace, debug, info, (warning/warn), error, critical)" );
	std::terminate();
}

spdlog::level::level_enum configureLoggingLevel()
{
	const auto level { idhan::config::get< std::string >( "logging", "level", "info" ) };
	spdlog::info( "Logging level: {}", level );

	const auto parsed { strToSpdlogLevel( level ) };
	spdlog::set_level( parsed );

	return parsed;
}

void checkSystemLocale()
{
	std::locale locale { "" };
	const auto name { locale.name() };
	idhan::log::debug( "Checking system locale" );
	idhan::log::info( "System locale: {}", name );

	const std::array< std::string_view, 2 > local_matches { { "UTF-8", "utf8" } };

	bool found_utf8 { false };
	for ( const auto& match : local_matches )
	{
		if ( name.find( match ) != std::string::npos )
		{
			found_utf8 = true;
			break;
		}
	}

	if ( name == "C" || name == "c" ) found_utf8 = true;

	if ( !found_utf8 )
	{
		idhan::log::critical( "System locale is not UTF8, Aborting (IDHAN must see UTF8 to work properly)" );
		std::terminate();
		FGL_UNREACHABLE();
	}
}

int main( int argc, char** argv )
{
	mallopt( M_ARENA_MAX, 2 );
	mallopt( M_MMAP_THRESHOLD, 1024 * 1024 * 128 );
	mallopt( M_MMAP_MAX, 0 );

	using namespace idhan;

	cli::Parser parser {
		options, format_ns::format( "IDHAN {}.{}.{}", IDHAN_MAJOR_VERSION, IDHAN_MINOR_VERSION, IDHAN_PATCH_VERSION )
	};

	parser.process( argc, argv );

	if ( parser.isSet( config_location ) )
	{
		const std::filesystem::path location { parser.value( config_location ) };
		idhan::config::setLocation( location );
	}

	applyCLISettings( "logging", "level", parser, log_level );

	applyCLISettings( "database", "host", parser, pg_host );

	applyCLISettings( "database", "user", parser, pg_user );

	applyCLISettings( "database", "schema", parser, pg_schema );

	applyCLISettings( "server", "use_stdout", parser, use_stdout );

	const auto level { configureLoggingLevel() };

	checkForceStart( parser, force_start );

	idhan::ConnectionArguments arguments {};
	arguments.log_level = level;

	if ( arguments.schema != idhan::db::DEFAULT_SCHEMA ) spdlog::info( "Using database schema: {}", arguments.schema );

	if ( arguments.use_stdout ) spdlog::info( "Using stdout for logging" );

	checkSystemLocale();

	log::info( "Starting IDHAN v{}.{}.{}", IDHAN_MAJOR_VERSION, IDHAN_MINOR_VERSION, IDHAN_PATCH_VERSION );

	idhan::ServerContext context { arguments };

	context.run();

	idhan::log::info( "Shutting down..." );

	return EXIT_SUCCESS;
}
