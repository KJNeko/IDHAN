//
// Created by kj16609 on 6/17/22.
//

// You may need to build the project (run Qt uic code generator) to get
// "ui_ImportViewer.h" resolved

// Ui
#include "importViewer.hpp"
#include "ImageDelegate.hpp"
#include "ui_importViewer.h"

// Qt
#include <QCryptographicHash>
#include <QFile>
#include <QFuture>
#include <QQueue>
#include <QThread>
#include <QtConcurrent/QtConcurrent>

// Logging/Prof
#include "TracyBox.hpp"

// Database
#include "database/FileData.hpp"
#include "database/databaseExceptions.hpp"
#include "database/files.hpp"
#include "database/metadata.hpp"

// std
#include <fstream>


#ifdef _WIN32
// TODO find an alternative to unistd fsync for windows
#elif __linux__


#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>


#else
#ifndef FGL_ACKNOWLEDGE_INCOMPATIBLE_OS
#error "Unsupported platform for IDHAN (Requirement: fsync implementation or similar) - please define FGL_ACKNOWLEDGE_INCOMPATIBLE_OS to fallback to std::ostream"
#endif
#endif


ImportViewer::ImportViewer( QWidget* parent ) : QWidget( parent ), ui( new Ui::ImportViewer )
{
	ui->setupUi( this );

	connect( this, &ImportViewer::updateValues, this, &ImportViewer::updateValues_slot );
	connect( this, &ImportViewer::addFileToView, this, &ImportViewer::addFileToView_slot );


	auto model = new ImageModel( this );
	auto delegate = new ImageDelegate( this );

	ui->listView->setModel( model );
	ui->listView->setItemDelegate( delegate );

	ui->listView->setItemAlignment( Qt::AlignCenter );
}


ImportViewer::~ImportViewer()
{

	if ( processingThread != nullptr )
	{ processingThread->cancel(); }


	delete ui;
}

/// TODO libFGL
#include <concepts>
#include <functional>
#include <type_traits>


namespace internal
{

	template< typename T, class T_ctor, class T_dtor, class... T_args > class raii_wrapper
	{
	public:
		T_dtor&& dtor;
		T val;


		raii_wrapper( T& val_, T_ctor& ctor_, T_dtor& dtor_, T_args... args_ )
			: dtor( std::move( dtor_ ) ), val( ctor_( std::forward< T_args >( args_ )... ) )
		{
			val_ = val;
		}


		~raii_wrapper()
		{
			std::invoke( dtor, val );
		}
	};




	/*template< typename T, class T_dtor > class raii_wrapper
	{
	public:
		T object;


		[[nodiscard]] constexpr explicit raii_wrapper(
			T&& obj, T_dtor&& dtor ) noexcept( noexcept( T( std::forward< decltype( obj ) >( obj ) ) ) &&
			noexcept( std::move( T_dtor( dtor ) ) ) )
			: object( std::forward< decltype( obj ) >( obj ) ), m_dtor( std::move( dtor ) )
		{
		}


		~raii_wrapper() noexcept( noexcept( std::invoke( m_dtor, object ) ) )
		{
			std::invoke( m_dtor, object );
		}


	private:
		T_dtor&& m_dtor;
	};*/

} // namespace internal


void ImportViewer::processFiles()
{
	ZoneScoped;

	QThreadPool pool;
	pool.setMaxThreadCount( 8 );

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
				emit addFileToView( *valID );
				return;
			}
			else
			{
				// TODO implement error for unknown import
			}
		}
		else if ( auto e = std::get_if< IDHANError >( &var ); e != nullptr )
		{
			auto val = *e;

			spdlog::warn( val.what() );

			if ( e->error_code_ == ErrorNo::DATABASE_DATA_NOT_FOUND )
			{
				++failed;
			}
			else if ( e->error_code_ == ErrorNo::DATABASE_DATA_ALREADY_EXISTS )
			{
				++alreadyinDB;
			}
			else if ( e->error_code_ == ErrorNo::FILE_NOT_FOUND )
			{
				++failed;
				return;
			}
			else
			{
				spdlog::critical( "Unhanbled error code: {}", static_cast<int>(e->error_code_) );
				throw val;
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

			// Check if the database already has the file we are about to
			// import
			uint64_t hash_id { 0 };

			QMimeDatabase mime_db;

			const auto mime_type = mime_db.mimeTypeForFile( path_.c_str() );

			// Generate filepath location from hash
			const std::filesystem::path filepath = getFilepathFromHash( sha256 ).string() +
				"." +
				mime_type.preferredSuffix().toStdString();

			// Check if file already exists
			if ( std::filesystem::exists( filepath ) )
			{
				spdlog::error( "File {} already exists", filepath.string() );
			}
			else
			{
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

				int val { ::open( filepath.c_str(), O_WRONLY | O_CREAT | O_SYNC | O_LARGEFILE, S_IRUSR | S_IWUSR ) };

				if ( val == -1 )
				{
					IDHANError err { ErrorNo::FSYNC_FILE_OPENEN_FAILED,
						"Failed to open file error code: " + std::to_string( val ) };

					return Output( err );
				}

				::write( val, bytes.data(), bytes.size() );

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

			hash_id = getFileID( sha256 );


			if ( hash_id )
			{
				//We got an ID. Throw FileExists
				throw IDHANError(
					ErrorNo::DATABASE_DATA_ALREADY_EXISTS, "hash_id: " + std::to_string( hash_id )
				);
			}

			hash_id = addFile( sha256 );

			//Add metadata
			populateMime( hash_id, mime_type.name().toStdString() );

			return Output( hash_id );
		}
		catch ( IDHANError& e )
		{
			return Output( e );
		}

	};

	auto future = QtConcurrent::mappedReduced(
		&pool, files, process, reduce
	);

	while ( !future.isFinished() )
	{
		QThread::yieldCurrentThread();
		QThread::msleep( 100 );
		emit updateValues();
	}
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


void ImportViewer::addFileToView_slot( uint64_t hash_id )
{
	auto model = dynamic_cast<ImageModel*>( ui->listView->model());
	model->addImage( hash_id );
}