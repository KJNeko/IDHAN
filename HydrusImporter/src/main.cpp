//
// Created by kj16609 on 11/2/24.
//

#include <QCommandLineParser>
#include <QCoreApplication>

#include <stdexcept>

int main( int argc, char** argv )
{
	QCoreApplication app { argc, argv };

	const auto& args { app.arguments() };

	QCommandLineParser parser;

	return EXIT_SUCCESS;
}
