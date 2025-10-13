//
// Created by kj16609 on 7/23/24.
//

#include <QCommandLineParser>

#include <cstdlib>

#include "ConnectionArguments.hpp"
#include "ServerContext.hpp"
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

int main( int argc, char** argv )
{
	using namespace idhan;

	QCommandLineParser parser {};
	parser.addHelpOption();
	parser.addVersionOption();

	QCommandLineOption log_level { "log_level",
		                           "Dictates the log level used (trace, debug, info, warning, error, critical",
		                           "level" };
	log_level.setDefaultValue( "info" );
	parser.addOption( log_level );

	QCommandLineOption pg_user { "pg_user", "The user to connect to the database with (default: 'idhan')", "pg_user" };
	pg_user.setDefaultValue( "idhan" );
	parser.addOption( pg_user );

	QCommandLineOption pg_host { "pg_host",
		                         "The host to connect to the database with (default: 'localhost')",
		                         "pg_host" };
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

	QCommandLineOption localhost_only { "localhost_only", "Only listens on localhost", "localhost_only" };
	localhost_only.setDefaultValue( "true" );
	parser.addOption( localhost_only );

	QCoreApplication app { argc, argv };
	app.setApplicationName( "IDHAN" );

	parser.process( app );

	applyCLISettings( "database", "hostname", parser, pg_host );

	idhan::ConnectionArguments arguments {};

	if ( parser.isSet( config_location ) )
	{
		const std::filesystem::path location { parser.value( config_location ).toStdString() };
		idhan::config::setLocation( location );
	}

	const auto strToSpdlogLevel = []( const std::string& level )
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
	};

	if ( !parser.isSet( log_level ) )
	{
		const auto level { idhan::config::get< std::string >( "logging", "level", "info" ) };
		spdlog::info( "Logging level: {}", level );

#ifdef NDEBUG
		arguments.log_level = strToSpdlogLevel( level );
#else
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

	if ( parser.isSet( use_testmode ) )
	{
		spdlog::warn( "Using testmode" );
		arguments.testmode = true;
	}

	if ( parser.isSet( use_stdout ) && ( parser.value( use_stdout ).toInt() == 0 ) )
	{
		arguments.use_stdout = false;
	}
	else
	{
		spdlog::info( "Using stdout for logging" );
	}

	if ( parser.isSet( localhost_only ) )
	{
		const auto parser_value = parser.value( localhost_only );

		if ( parser_value == "false" || parser_value == "0" )
		{
			arguments.listen_localhost_only = false;
		}
		else
		{
			arguments.listen_localhost_only = true;
		}
	}

	idhan::ServerContext context { arguments };

	context.run();

	idhan::log::info( "Shutting down..." );

	return EXIT_SUCCESS;
}
