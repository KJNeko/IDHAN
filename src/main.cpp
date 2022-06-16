//
// Created by kj16609 on 6/1/22.
//

#include <iostream>

#include "database.hpp"
#include "./services/thumbnailer.hpp"

#include <TracyBox.hpp>

#include <QtCore>
#include <QApplication>
#include <QWidget>

#include "mainView.hpp"

void resetDB()
{
	ConnectionRevolver::resetDB();
}

int main(int argc, char** argv)
{
	std::cout << "Qt version: " << qVersion() << std::endl;
	
	idhan::services::ImageThumbnailer::start();
	idhan::services::Thumbnailer::start();
	
	//VIPS
	if(VIPS_INIT(argv[0]))
	{
		throw std::runtime_error("Failed to initialize vips");
	}
	
	QApplication app(argc, argv);
	
	MainWindow window;
	window.show();
	
	
	TracyCZoneN(await,"Shutdown",true);
	idhan::services::ImageThumbnailer::await();
	idhan::services::Thumbnailer::await();
	TracyCZoneEnd(await);
	
	//VIPS
	vips_shutdown();
	
	
	return app.exec();
}