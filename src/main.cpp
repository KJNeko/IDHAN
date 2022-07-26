//
// Created by kj16609 on 6/1/22.
//

#include "TracyBox.hpp"


#ifdef TRACY_ENABLE


void* operator new( std::size_t count )
{
	auto ptr = malloc( count );
	TracySecureAlloc ( ptr, count );
	return ptr;
}


void* operator new[]( std::size_t count )
{
	auto ptr = malloc( count );
	TracySecureAlloc ( ptr, count );
	return ptr;
}


void operator delete( void* ptr ) noexcept
{
	TracySecureFree ( ptr );
	free( ptr );
}


void operator delete[]( void* ptr ) noexcept
{
	TracySecureFree ( ptr );
	free( ptr );
}


void operator delete( void* ptr, std::size_t ) noexcept
{
	TracySecureFree ( ptr );
	free( ptr );
}


void operator delete[]( void* ptr, std::size_t ) noexcept
{
	TracySecureFree ( ptr );
	free( ptr );
}


#endif


#include <iostream>

#include <QApplication>
#include <QtCore>
#include <QPixmapCache>
#include <QImageReader>

#include "views/IDHANView/IDHANView.hpp"

#include "database/database.hpp"

#include "idhan_systems/threading.hpp"


void customMessageHandler( QtMsgType type, const QMessageLogContext& context, const QString& msg )
{
	QByteArray localMsg = msg.toLocal8Bit();
	const char* file = context.file ? context.file : "";
	const char* function = context.function ? context.function : "";
	switch ( type )
	{
	case QtDebugMsg:
		spdlog::debug( "{} ({}:{}, {})", localMsg.constData(), file, context.line, function );
		break;
	case QtInfoMsg:
		spdlog::info( "{} ({}:{}, {})", localMsg.constData(), file, context.line, function );
		break;
	case QtWarningMsg:
		spdlog::warn( "{} ({}:{}, {})", localMsg.constData(), file, context.line, function );
		break;
	case QtCriticalMsg:
		spdlog::critical( "{} ({}:{}, {})", localMsg.constData(), file, context.line, function );
		break;
	case QtFatalMsg:
		spdlog::critical( "{} ({}:{}, {})", localMsg.constData(), file, context.line, function );
		break;
	}

}


int main( int argc, char** argv )
{
	spdlog::info( "Qt version: {}", qVersion() );

	spdlog::info( "Starting IDHAN" );
	QImageReader::setAllocationLimit( 0 );

	spdlog::info( "Attaching custom handler to Qt messages" );
	//qInstallMessageHandler( customMessageHandler );
	spdlog::info( "Attached" );

	spdlog::info( "Initalizing thread pools" );
	initThreadPools();

	QPixmapCache::setCacheLimit( 1000000 ); //1 = 1KB
	spdlog::info( "QPixmapCache limit set to {} KB", QPixmapCache::cacheLimit() );

	QApplication app( argc, argv );

	QCoreApplication::setOrganizationName( "Future Gadget Labs" );
	QCoreApplication::setApplicationName( "IDHAN" );

	QSettings s( QSettings::IniFormat, QSettings::UserScope, "Future Gadget Labs", "IDHAN" );

	if ( s.value( "firstRun", true ).toBool() )
	{
		s.setValue( "paths/thumbnail_path", "./db/thumbnails" );
		s.setValue( "paths/file_path", "./db/files" );
		s.setValue( "firstRun", false );
	}

	if ( !s.value( "QtSilence", false ).toBool() )
	{
		spdlog::warn(
			"QtDebug is set to false. This silences qt.gui.icc and qt.gui.imageio. This is intentional. set QtSilence in {} to false to enable them again", s
			.fileName()
			.toStdString()
		);
		QLoggingCategory::setFilterRules( QStringLiteral( "qt.gui.icc=false" ) );
		QLoggingCategory::setFilterRules( QStringLiteral( "qt.gui.imageio=false" ) );
	}

	spdlog::set_level( spdlog::level::debug );

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


	spdlog::info( "Rendering window" );
	MainWindow window;
	window.show();

	auto ret { app.exec() };

	spdlog::info( "Window closed...Cleaning up" );


	return ret;
}