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

	QCommandLineOption use_stdout_option { "use_stdout",
		                                   "Enables the logger to output to stdout (Default: 1)",
		                                   "flag" };
	use_stdout_option.setDefaultValue( "1" );
	parser.addOption( use_stdout_option );

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

	QCoreApplication app { argc, argv };
	app.setApplicationName( "IDHAN" );

	parser.process( app );

	spdlog::set_level( spdlog::level::debug );

	idhan::ConnectionArguments arguments {};
	arguments.user = parser.value( pg_user ).toStdString();
	arguments.hostname = parser.value( pg_host ).toStdString();

	if ( !parser.isSet( "log_level" ) )
	{
		spdlog::set_level( spdlog::level::info );
	}
	else
	{
		const auto level { parser.value( log_level ).toStdString() };

		if ( level == "trace" )
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

#ifndef NDEBUG
	if ( parser.isSet( testmode_option ) )
	{
		arguments.testmode = true;
	}
#else
	arguments.testmode = false;
#endif

	if ( parser.value( use_stdout_option ).toInt() > 0 )
	{
		arguments.use_stdout = true;
	}

	idhan::ServerContext context { arguments };

	context.run();

	idhan::log::info( "Shutting down..." );

	return EXIT_SUCCESS;
}
