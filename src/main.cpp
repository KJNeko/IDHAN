//
// Created by kj16609 on 6/1/22.
//

#include <iostream>

#include <QApplication>
#include <QtCore>

#include "mainView.hpp"

#include "TracyBox.hpp"

int main( int argc, char** argv )
{
	try
	{
		std::cout << "Running" << std::endl;
		
		std::cout << "Qt version: " << qVersion() << std::endl;
		
		QApplication app( argc, argv );
		
		MainWindow window;
		window.show();
		
		return app.exec();
	}
	catch(std::exception& e)
	{
		std::cout << e.what() << std::endl;
	}
	catch(...)
	{
		std::cout << "wtf" << std::endl;
	}
}