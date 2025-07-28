//
// Created by kj16609 on 7/27/25.
//

#include <QApplication>

#include "WebUI.hpp"

QApplication* app { nullptr };
idhan::WebUI* webui { nullptr };

int main( int argc, char** argv )
{
	app = new QApplication( argc, argv );
	webui = new idhan::WebUI();

	webui->show();

	return EXIT_SUCCESS;
}