//
// Created by kj16609 on 6/17/22.
//

// You may need to build the project (run Qt uic code generator) to get
// "ui_ImportViewer.h" resolved

// Ui
#include "importViewer.hpp"
#include "ui_importViewer.h"

// Qt
#include <QCryptographicHash>
#include <QFuture>
#include <QQueue>
#include <QThread>
#include <QtConcurrent/QtConcurrent>

// Logging/Prof
#include "TracyBox.hpp"

// Database
#include "database/utility/databaseExceptions.hpp"
#include "database/files/files.hpp"
#include "database/files/metadata.hpp"
#include "database/tags/mappings.hpp"

// std
#include <fstream>

#include "listViewport.hpp"
#include "TagView.hpp"


#ifdef _WIN32
// TODO find an alternative to unistd fsync for windows
#elif __linux__


#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>


#else
#ifndef FGL_ACKNOWLEDGE_INCOMPATIBLE_OS
#error "Unsupported platform for IDHAN (Requirement: fsync implementation or similar) - please define FGL_ACKNOWLEDGE_INCOMPATIBLE_OS to fallback to std::ostream"
#endif
#endif


ImportViewer::ImportViewer( QWidget* parent ) : QWidget( parent ), ui( new Ui::ImportViewer )
{
	ui->setupUi( this );

	viewport = new ListViewport( this );
	tagport = new TagView( this );

	ui->viewFrame->layout()->addWidget( viewport );

	ui->tagFrame->layout()->addWidget( tagport );

	//Connect all the things
	connect( this, &ImportViewer::updateValues, this, &ImportViewer::updateValues_slot );

	//Connect the selection model to the tagview
	connect( viewport, &ListViewport::selection, tagport, &TagView::selectionChanged );
}


ImportViewer::~ImportViewer()
{

	if ( processingThread != nullptr )
	{ processingThread->cancel(); }

	delete ui;
}


void ImportViewer::processFiles()
{
	ZoneScoped;

	QThreadPool pool;
	pool.setMaxThreadCount( 1 );

	using Output = std::variant< uint64_t, IDHANError >;

	//Apparently reduce needs a first parameter of a 'return' value for whatever reason
	auto reduce = [ this ]( [[maybe_unused]] uint64_t&, const Output& var ) -> void
	{
		ZoneScoped;
		++filesProcessed;

		if ( auto valID = std::get_if< uint64_t >( &var ); valID != nullptr )
		{
			if ( *valID != 0 )
			{
				++successful;
				viewport->addFile( *valID );
				return;
			}
			else
			{
				// TODO implement error for unknown import
			}
		}
		else if ( auto e = std::get_if< IDHANError >( &var ); e != nullptr )
		{
			auto& val = *e;

			if ( val.error_code_ == ErrorNo::DATABASE_DATA_NOT_FOUND )
			{
				++failed;
			}
			else if ( val.error_code_ == ErrorNo::DATABASE_DATA_ALREADY_EXISTS )
			{
				if ( val.hash_id == 0 )
				{
					++failed;
					return;
				}
				viewport->addFile( val.hash_id );
				++alreadyinDB;
				return;
			}
			else if ( val.error_code_ == ErrorNo::FILE_NOT_FOUND )
			{
				++failed;
				return;
			}
			else if ( val.error_code_ == ErrorNo::UNKNOWN_ERROR )
			{
				spdlog::critical( "Unknown error occurred while processing file! error: {}", e->what() );
			}
			else
			{
				spdlog::critical( "Unhandled error code: {}", static_cast<int>(val.error_code_) );
			}
		}
		else
		{ throw std::runtime_error( "Unknown variant type" ); }


		return;
	};

	auto process = []( std::filesystem::path path_ ) -> Output
	{
		ZoneScopedN( "process" );
		try
		{
			if ( !std::filesystem::exists( path_ ) )
			{
				spdlog::error( "File {} does not exist", path_.string() );
				throw std::runtime_error( "File does not exist" );
			}

			const std::vector< std::byte > bytes = [ &path_ ]() -> std::vector< std::byte >
			{
				ZoneScopedN( "Read file" );
				if ( std::ifstream ifs( path_ ); ifs )
				{
					const auto size { std::filesystem::file_size( path_ ) };

					std::vector< std::byte > bytes_;
					bytes_.resize( size );

					// Read to buffer

					ifs.read(
						reinterpret_cast<char*>( bytes_.data()), static_cast<long>( size )
					);
					return bytes_;
				}

				return {};
			}();

			const QByteArrayView bytes_view(
				bytes.data(), static_cast<qsizetype>( bytes.size())
			);

			const Hash32 sha256 { QCryptographicHash::hash(
				bytes_view, QCryptographicHash::Sha256
			) };

			QMimeDatabase mime_db;

			const auto mime_type = mime_db.mimeTypeForFile( path_.c_str() );

			// Generate filepath location from hash
			std::filesystem::path filepath = getFilepathFromHash( sha256 ).string();
			const auto ext = mime_type.preferredSuffix().toStdString();

			if ( ext.size() > 0 )
			{
				filepath += ".";
				filepath += ext;
			}

			// Check if file already exists
			if ( !std::filesystem::exists( filepath ) )
			{
				//TODO implement "security" levels to check already written files

				// Ensure that the parent directory exists
				std::filesystem::create_directories( filepath.parent_path() );

				#ifdef __linux__

				// Notes
				// O_LARGEFILE can be used if the file would exceed the
				// off_t (32bit) limit in size

				// Active
				// O_CREAT creats the file if it does not exist
				// O_WRONLY opens the file for writing only
				// O_SYNC ensures that the data is written to disk before
				// returning from the write call

				int val { ::open( filepath.c_str(), O_WRONLY | O_CREAT | O_LARGEFILE, S_IRUSR | S_IWUSR ) };

				if ( val == -1 )
				{
					IDHANError err { ErrorNo::FSYNC_FILE_OPENEN_FAILED,
						"Failed to open file error code: " + std::to_string( val ) };

					return Output( err );
				}

				::write( val, bytes.data(), bytes.size() );


				//Start preparing the thumbnail while we wait for the data to be written

				TracyCZoneN( generate_thumbnail, "generate_thumbnail", true );
				QSettings s;
				const auto x_res = s.value( "thumbnails/x_res", 120 ).toInt();
				const auto y_res = s.value( "thumbnails/y_res", 120 ).toInt();

				if ( mime_type.name().contains( "image/" ) )
				{
					QImage raw;
					raw.loadFromData( reinterpret_cast<const unsigned char*>(bytes.data()), static_cast<int>(bytes.size()) );

					//Resize image
					const auto resized_qimage = raw.scaled( x_res, y_res, Qt::KeepAspectRatio, Qt::SmoothTransformation );

					auto thumbnail_path = getThumbnailpathFromHash( sha256 );

					//Ensure the parent directory is created
					std::filesystem::create_directories( thumbnail_path.parent_path() );

					resized_qimage.save( QString::fromStdString( thumbnail_path.string() ) );
				}
				TracyCZoneEnd( generate_thumbnail );

				//Ensure that everything is done
				::fsync( val );

				::close( val );

				#else
				// TODO implement for windows
				// https://docs.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-flushfilebuffers?redirectedfrom=MSDN


				#warning "Current operating system doesn't have fsync support implemented, This could cause dataloss in the event of powerloss or a crash"
				// Write the file
				// If the OS dies and your file isn't fully written.
				// Not my fault. Get a better OS bitch
				if ( std::ofstream ofs( filepath ); ofs )
				{
					ofs.write(
						reinterpret_cast<const char*>( bytes.data() ),
						static_cast<long>( bytes.size() ) );
					ofs.rdbuf()->pubsync();
				}
				#endif
			}


			// Insert into database
			//Check if it is in the database
			const uint64_t hash_id_exists = getFileID( sha256 );

			if ( hash_id_exists )
			{
				throw IDHANError( ErrorNo::DATABASE_DATA_ALREADY_EXISTS, "Duplicate hash_id", hash_id_exists );
			}

			const auto hash_id = addFile( sha256 );

			//Add metadata
			populateMime( hash_id, mime_type.name().toStdString() );

			//Add the "system:inbox" tag to the image
			addMapping( sha256, "system", "inbox" );

			//Check to see if a file exists with the tags
			if ( std::filesystem::exists( path_.string() + ".txt" ) )
			{
				spdlog::info( "Suspected file with tags found {}", path_.string() + ".txt" );

				if ( std::ifstream ifs( path_.string() + ".txt" ); ifs )
				{
					while ( !ifs.eof() )
					{
						std::string name;

						std::getline( ifs, name );

						std::vector< std::string > split_strings;

						constexpr std::string_view delim { ":" };

						const std::string_view sv { name };

						for ( const auto word: std::views::split( sv, delim ) )
						{
							if ( split_strings.empty() )
							{
								//Push back just the group
								split_strings.push_back( std::string( word.data(), word.size() ) );
							}
							else
							{
								//Push back the rest of the tag
								split_strings.push_back( std::string( word.data(), word.size() ) );
							}
							spdlog::debug( "Inserted: {}", split_strings.back() );
						}

						if ( split_strings.size() == 2 )
						{
							spdlog::debug( "split_strings.size() = {}", split_strings.size() );
							spdlog::debug( "split_strings[0] = {}, split_strings[1] = {}", split_strings[ 0 ], split_strings[ 1 ] );
							addMapping( sha256, split_strings[ 0 ], split_strings[ 1 ] );
						}
						else if ( split_strings.size() == 1 )
						{
							addMapping( sha256, "", split_strings[ 0 ] );
						}
						else
						{
							spdlog::error( "Invalid tag format {}", name );
						}

					};
				}
			}


			return Output( hash_id );

			spdlog::critical( "PROCESS HAS ESCAPED SCOPE BOUNDS!" );
		}
		catch ( IDHANError& e )
		{
			return Output( IDHANError( e.error_code_, e.what(), e.hash_id ) );
		}
		catch ( std::exception& e )
		{
			spdlog::error( "Process: {}", e.what() );
			return Output( IDHANError( ErrorNo::UNKNOWN_ERROR, e.what() ) );
		}

		spdlog::critical( "PROCESS HAS ESCAPED SCOPE BOUNDS!" );
	};

	try
	{
		auto future = QtConcurrent::mappedReduced(
			&pool, files, process, reduce
		);

		while ( !future.isFinished() && !future.isCanceled() )
		{
			QThread::yieldCurrentThread();
			QThread::msleep( 100 );
			emit updateValues();
		}
	}
	catch ( const std::exception& e )
	{
		spdlog::error( e.what() );
		throw e;
	}


	spdlog::info( "Finished processing files" );
}


void ImportViewer::updateValues_slot()
{
	ZoneScoped;
	QString str;

	str += QString( "%1/%2" ).arg( filesProcessed ).arg( filesAdded );

	// Set the label
	ui->progressLabel->setText( str );

	// Set the progress bar
	ui->progressBar->setValue( static_cast<int>( filesProcessed ) );

	QString str2;

	str2 += successful ? QString( "%1 successful " ).arg( successful ) : "";
	str2 += failed && successful ? QString( "%1 failed (" ).arg( failed ) : "";
	str2 += alreadyinDB ? QString( "%1 already in DB " ).arg( alreadyinDB ) : "";
	str2 += deleted ? QString( "%1 deleted " ).arg( deleted ) : "";
	str2 += failed && successful ? QString( ")" ) : "";


	ui->statusLabel->setText( str2 );
}


void ImportViewer::addFiles( const std::vector< std::filesystem::path >& files_ )
{
	ZoneScoped;
	files.reserve( files_.size() );
	files = files_;

	filesAdded = files.size();

	QString str;

	str += QString( "%1/%2" ).arg( filesProcessed ).arg( filesAdded );

	// Set the label
	ui->progressLabel->setText( str );

	ui->progressBar->setMaximum( static_cast<int>( filesAdded ) );

	auto runfuture = QtConcurrent::run(
		QThreadPool::globalInstance(), &ImportViewer::processFiles, this
	);
}

