//
// Created by kj16609 on 7/23/24.
//

#include <QCommandLineParser>
// FUCKING QT IS RETARDED
#undef signals

#include <cstdlib>

#include "ConnectionArguments.hpp"
#include "ServerContext.hpp"
#include "fgl/defines.hpp"
#include "logging/log.hpp"

void applyCLISettings(
	const std::string_view group,
	const std::string_view name,
	const QCommandLineParser& parser,
	const QCommandLineOption& pg_host )
{
	using namespace idhan::config;
	if ( parser.isSet( pg_host ) )
	{
		addCLIConfig( group, name, parser.value( pg_host ).toStdString() );
	}
}

void checkForceStart( const QCommandLineParser& parser, const QCommandLineOption& force_start )
{
	if ( parser.isSet( force_start ) && parser.value( force_start ) == "true" )
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
	// invalid level, throw
	spdlog::critical( "Invalid log level, Expected one of: (trace, debug, info, (warning/warn), error, critical)" );
	std::terminate();
}

void configureLoggingLevel(
	const QCommandLineParser& parser,
	const QCommandLineOption& log_level,
	idhan::ConnectionArguments& arguments )
{
	if ( !parser.isSet( log_level ) )
	{
		const auto level { idhan::config::get< std::string >( "logging", "level", "info" ) };

#ifdef NDEBUG
		spdlog::info( "Logging level: {}", level );
		arguments.log_level = strToSpdlogLevel( level );
#else
		spdlog::info( "Logging level: debug" );
		arguments.log_level = spdlog::level::debug;
#endif

		spdlog::set_level( arguments.log_level );
	}
	else
	{
		const auto level { parser.value( log_level ).toStdString() };
		spdlog::info( "Logging level: {}", level );
		spdlog::set_level( strToSpdlogLevel( level ) );
		arguments.log_level = strToSpdlogLevel( level );
	}
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
	using namespace idhan;

	QCommandLineParser parser {};
	parser.addHelpOption();
	parser.addVersionOption();

	QCommandLineOption log_level {
		"log_level", "Dictates the log level used (trace, debug, info, warning, error, critical", "level"
	};
	log_level.setDefaultValue( "info" );
	parser.addOption( log_level );

	QCommandLineOption pg_user { "pg_user", "The user to connect to the database with (default: 'idhan')", "pg_user" };
	pg_user.setDefaultValue( "idhan" );
	parser.addOption( pg_user );

	QCommandLineOption pg_host {
		"pg_host", "The host to connect to the database with (default: 'localhost')", "pg_host"
	};
	pg_host.setDefaultValue( "localhost" );
	parser.addOption( pg_host );

	QCommandLineOption use_stdout { "use_stdout", "Use stdout for logging", "use_stdout" };
	use_stdout.setDefaultValue( "1" );
	parser.addOption( use_stdout );

	QCommandLineOption use_testmode { "testmode", "Forces the DB to use the `test` schema", "testmode" };
	parser.addOption( use_testmode );

	QCommandLineOption config_location { "config", "The location for the config file", "config_location" };
	config_location.setDefaultValue( "./config.json" );
	parser.addOption( config_location );

	QCommandLineOption force_start {
		"force_start", "Forces IDHAN to start even if it thinks it shouldn't", "force_start"
	};
	force_start.setDefaultValue( "false" );
	parser.addOption( force_start );

	QCoreApplication app { argc, argv };
	app.setApplicationName( "IDHAN" );

	parser.process( app );

	applyCLISettings( "database", "hostname", parser, pg_host );

	checkForceStart( parser, force_start );

	idhan::ConnectionArguments arguments {};

	if ( parser.isSet( config_location ) )
	{
		const std::filesystem::path location { parser.value( config_location ).toStdString() };
		idhan::config::setLocation( location );
	}

	configureLoggingLevel( parser, log_level, arguments );

	arguments.testmode |= parser.isSet( use_testmode );

	if ( arguments.testmode ) spdlog::warn( "Using testmode" );

	if ( parser.isSet( use_stdout ) && ( parser.value( use_stdout ).toInt() == 0 ) )
	{
		arguments.use_stdout = false;
	}

	if ( arguments.use_stdout ) spdlog::info( "Using stdout for logging" );

	// Terminates if locale is bad
	checkSystemLocale();

	log::info( "Starting IDHAN v{}.{}.{}", IDHAN_MAJOR_VERSION, IDHAN_MINOR_VERSION, IDHAN_PATCH_VERSION );

	idhan::ServerContext context { arguments };

	context.run();

	idhan::log::info( "Shutting down..." );

	return EXIT_SUCCESS;
}
