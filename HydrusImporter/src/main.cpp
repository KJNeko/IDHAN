//
// Created by kj16609 on 11/2/24.
//

#include <QCommandLineParser>
#include <QCoreApplication>

#include "NET_CONSTANTS.hpp"
#include "idhan/IDHANClient.hpp"

int main( int argc, char** argv )
{
	QCoreApplication app { argc, argv };
	app.setApplicationName( "1.0" );

	QCommandLineParser parser {};
	parser.addHelpOption();
	parser.addVersionOption();

	QCommandLineOption idhan_host { { "H", "host" }, "Hostname or IP of the IDHAN server" };
	idhan_host.setDefaultValue( "localhost" );
	QCommandLineOption idhan_port { { "P", "port" }, "Port of the IDHAN server" };
	idhan_port.setDefaultValue( QString::number( idhan::IDHAN_DEFAULT_PORT ) );

	parser.addPositionalArgument( "hydrus db", "Points to the hydrus db directory\nExample: '~/Desktop/hydrus/db'" );

	parser.addOptions( { idhan_host, idhan_port } );

	parser.process( app );

	idhan::IDHANClientConfig config {};

	return EXIT_SUCCESS;
}
