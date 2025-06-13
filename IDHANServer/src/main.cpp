//
// Created by kj16609 on 7/23/24.
//

#include <QCommandLineParser>

#include <cstdlib>

#include "ConnectionArguments.hpp"
#include "ServerContext.hpp"
#include "logging/log.hpp"

int main( int argc, char** argv )
{
	QCommandLineParser parser {};
	parser.addHelpOption();
	parser.addVersionOption();

#ifndef NDEBUG

	QCommandLineOption testmode_option { "testmode", "Enables testmode if present" };
	parser.addOption( testmode_option );

#endif

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

	QCommandLineOption config_location { "config", "The location for the config file", "config_location" };
	config_location.setDefaultValue( "./config.json" );
	parser.addOption( config_location );

	QCoreApplication app { argc, argv };
	app.setApplicationName( "IDHAN" );

	parser.process( app );

	idhan::ConnectionArguments arguments {};
	arguments.user = parser.value( pg_user ).toStdString();
	arguments.hostname = parser.value( pg_host ).toStdString();

	if ( parser.isSet( config_location ) )
	{
		const std::filesystem::path location { parser.value( config_location ).toStdString() };
		idhan::config::setLocation( location );
	}

	if ( !parser.isSet( log_level ) )
	{
		const auto level { idhan::config::get< std::string >( "logging", "level", "info" ) };

		if ( level == "trace" )
			spdlog::set_level( spdlog::level::trace );
		else if ( level == "trace" )
			spdlog::set_level( spdlog::level::trace );
		else if ( level == "debug" )
			spdlog::set_level( spdlog::level::debug );
		else if ( level == "info" )
			spdlog::set_level( spdlog::level::info );
		else if ( level == "warning" || level == "warn" )
			spdlog::set_level( spdlog::level::warn );
		else if ( level == "error" )
			spdlog::set_level( spdlog::level::err );
		else if ( level == "critical" )
			spdlog::set_level( spdlog::level::critical );
		else
		{
			// invalid level, throw
			spdlog::
				critical( "Invalid log level, Expected one of: (trace, debug, info, (warning/warn), error, critical)" );
			std::terminate();
		}
	}
	else
	{
		const auto level { parser.value( log_level ).toStdString() };

		if ( level == "trace" )
			spdlog::set_level( spdlog::level::trace );
		else if ( level == "trace" )
			spdlog::set_level( spdlog::level::trace );
		else if ( level == "debug" )
			spdlog::set_level( spdlog::level::debug );
		else if ( level == "info" )
			spdlog::set_level( spdlog::level::info );
		else if ( level == "warning" || level == "warn" )
			spdlog::set_level( spdlog::level::warn );
		else if ( level == "error" )
			spdlog::set_level( spdlog::level::err );
		else if ( level == "critical" )
			spdlog::set_level( spdlog::level::critical );
		else
		{
			// invalid level, throw
			spdlog::
				critical( "Invalid log level, Expected one of: (trace, debug, info, (warning/warn), error, critical)" );
			std::terminate();
		}
	}

	spdlog::trace( "Logging level trace" );
	spdlog::debug( "Logging level debug" );
	spdlog::info( "Logging level info" );

#ifndef NDEBUG
	if ( parser.isSet( testmode_option ) )
	{
		arguments.testmode = true;
	}
#else
	arguments.testmode = false;
#endif

	idhan::ServerContext context { arguments };

	context.run();

	idhan::log::info( "Shutting down..." );

	return EXIT_SUCCESS;
}
