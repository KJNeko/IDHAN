//
// Created by kj16609 on 6/1/22.
//

#include <iostream>

#include <QApplication>
#include <QtCore>

#include "mainView.hpp"

#include "TracyBox.hpp"

#include "database.hpp"
#include "database/idhanDatabase.hpp"

int main( int argc, char** argv )
{
	{
		Database db;
		db.init();
	}

	Hash hash;


	const QByteArray hash_var = QByteArray::fromRawData( (const char*)hash.data(), hash.size() );
	const QString hash_hex	  = hash_var.toHex();
	const std::string view	  = hash_hex.toStdString();

	std::cout << view << std::endl;

	std::cout << "Id: " << getFileID( hash, true ) << std::endl;

	std::cout << "Qt version: " << qVersion() << std::endl;

	QApplication app( argc, argv );

	MainWindow window;
	window.show();

	return app.exec();
}