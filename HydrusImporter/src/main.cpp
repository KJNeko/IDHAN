//
// Created by kj16609 on 11/2/24.
//

#include <QApplication>
#include <QCoreApplication>

#include "gui/main/MainWindow.hpp"
#include "idhan/IDHANClient.hpp"

int main( int argc, char** argv )
{
	QApplication app { argc, argv };
	app.setApplicationName( "IDHAN Importer" );

	// Clobber the default locale because whatever
	QLocale locale( QLocale::English );
	locale.setNumberOptions( QLocale::DefaultNumberOptions );
	QLocale::setDefault( locale );

	MainWindow window {};

	window.show();

	return app.exec();
}
