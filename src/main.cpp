//
// Created by kj16609 on 6/1/22.
//

#include <iostream>

#include <QApplication>
#include <QtCore>

#include "mainView.hpp"

#include "TracyBox.hpp"

#include "database.hpp"

int main( int argc, char** argv )
{
	std::cout << "Qt version: " << qVersion() << std::endl;

	QApplication app( argc, argv );

	QCoreApplication::setOrganizationName( "Future Gadget Labs" );
	QCoreApplication::setApplicationName( "IDHAN" );

	QSettings s;

	spdlog::info( "IDHAN config location: " + s.fileName().toStdString() );

	s.beginGroup( "Database" );

	const std::string db_host =
		s.value( "host", "localhost" ).toString().toStdString();
	const std::string db_name = s.value( "name", "idhan" ).toString().toStdString();
	const std::string db_user = s.value( "user", "idhan" ).toString().toStdString();
	const std::string db_pass = s.value( "pass", "idhan" ).toString().toStdString();

	Database::initalizeConnection(
		"host=" + db_host + " dbname=" + db_name + " user=" + db_user +
		" password=" + db_pass );


	MainWindow window;
	window.show();

	auto ret { app.exec() };

	if ( s.value( "firstRun", true ).toBool() )
	{
		s.setValue( "firstRun", false );
	}

	return ret;
}