//
// Created by kj16609 on 11/2/24.
//

#include <QCommandLineParser>
#include <QCoreApplication>

#include <filesystem>
#include <iostream>

#include "HydrusImporter.hpp"
#include "NET_CONSTANTS.hpp"
#include "idhan/IDHANClient.hpp"

int main( int argc, char** argv )
{
	QCommandLineParser parser {};
	parser.addHelpOption();
	parser.addVersionOption();

	QCommandLineOption idhan_host { { "H", "host" }, "Hostname or IP of the IDHAN server" };
	idhan_host.setDefaultValue( "localhost" );
	idhan_host.setValueName( "idhan_host" );

	QCommandLineOption idhan_port { { "P", "port" }, "Port of the IDHAN server" };
	idhan_port.setDefaultValue( QString::number( idhan::IDHAN_DEFAULT_PORT ) );
	idhan_port.setValueName( "idhan_port" );

	QCommandLineOption process_ptr { "process_ptr", "Process the PTR mappings of files" };
	process_ptr.setDefaultValue( "0" );
	process_ptr.setValueName( "1/0" );
	process_ptr.setDescription(
		"Processes the PTR mappings from Hydrus, This can take an EXTREMELY LONG TIME. Default false (0)" );

	parser.addPositionalArgument( "hydrus_db", "Points to the hydrus db directory\nExample: '~/Desktop/hydrus/db'" );

	parser.addOptions( { idhan_host, idhan_port, process_ptr } );

	QCoreApplication app { argc, argv };
	app.setApplicationName( "Hydrus Importer" );

	parser.process( app );

	const auto pos_args { parser.positionalArguments() };
	if ( pos_args.size() != 1 )
	{
		std::cout << "Mismatched number of arguments. Expected 1" << std::endl;
		parser.showHelp();
		std::terminate();
	}

	const std::filesystem::path hydrus_path { pos_args[ 0 ].toStdString() };
	if ( !std::filesystem::exists( hydrus_path ) )
	{
		std::cout << "Path supplied does not exist" << std::endl;
		parser.showHelp();
		std::terminate();
	}

	if ( !std::filesystem::is_directory( hydrus_path ) )
	{
		std::cout << "Path is not a directory" << std::endl;
		parser.showHelp();
		std::terminate();
	}

	idhan::IDHANClientConfig config {};

	config.hostname = parser.value( idhan_host ).toStdString();
	config.port = parser.value( idhan_port ).toUShort();

	std::shared_ptr< idhan::IDHANClient > client { std::make_shared< idhan::IDHANClient >( config ) };

	const bool process_ptr_flag { parser.value( process_ptr ).toInt() > 0 ? true : false };

	auto hydrus_importer { std::make_shared< idhan::hydrus::HydrusImporter >( hydrus_path, client, process_ptr_flag ) };

	hydrus_importer->copyHydrusInfo();

	return app.exec();
}
