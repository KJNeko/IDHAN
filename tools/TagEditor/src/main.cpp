#include <QApplication>

#include "ui/MainWindow.hpp"

int main( int argc, char** argv )
{
	QApplication app { argc, argv };

	MainWindow window;

	window.show();

	app.exec();
}
