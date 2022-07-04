//
// Created by kj16609 on 6/1/22.
//

#include <iostream>

#include <QApplication>
#include <QtCore>

#include "mainView.hpp"

#include "TracyBox.hpp"

#include "database.hpp"

#include "database/files.hpp"


int main( int argc, char** argv )
{
	std::cout << "Qt version: " << qVersion() << std::endl;

	QApplication app( argc, argv );

	QCoreApplication::setOrganizationName( "Future Gadget Labs" );
	QCoreApplication::setApplicationName( "IDHAN" );

	QSettings s;

	if ( s.value( "firstRun", true ).toBool() )
	{
		s.setValue( "paths/thumbnail_path", "./db/thumbnails" );
		s.setValue( "paths/file_path", "./db/files" );
		s.setValue( "firstRun", false );
	}

	//Create directories if they don't exist. error if unable to access or make directories
	QDir dir;

	if ( !dir.exists( s.value( "paths/thumbnail_path" ).toString() ) )
	{
		if ( !dir.mkpath( s.value( "paths/thumbnail_path" ).toString() ) )
		{
			spdlog::critical( "Unable to create thumbnail directory or access it" );
			return 1;
		}
	}

	if ( !dir.exists( s.value( "paths/file_path" ).toString() ) )
	{
		if ( !dir.mkpath( s.value( "paths/file_path" ).toString() ) )
		{
			spdlog::critical( "Unable to create file directory or access it" );
			return 1;
		}
	}

	spdlog::info( "IDHAN config location: " + s.fileName().toStdString() );

	s.beginGroup( "Database" );

	const std::string db_host = s.value( "host", "localhost" ).toString().toStdString();
	const std::string db_name = s.value( "name", "idhan" ).toString().toStdString();
	const std::string db_user = s.value( "user", "idhan" ).toString().toStdString();
	const std::string db_pass = s.value( "pass", "idhan" ).toString().toStdString();

	Database::initalizeConnection(
		"host=" + db_host + " dbname=" + db_name + " user=" + db_user + " password=" + db_pass
	);


	MainWindow window;
	window.show();

	auto ret { app.exec() };


	return ret;
}