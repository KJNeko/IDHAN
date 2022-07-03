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

	connect( delegate, &ImageDelegate::generateThumbnail, model, &ImageModel::generateThumbnail );
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
	template< typename T, class T_dtor > class raii_wrapper
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
	};
} // namespace internal


void ImportViewer::processFiles()
{
	ZoneScoped;

	QThreadPool pool;
	pool.setMaxThreadCount( 8 );

	using Output = std::variant< uint64_t, std::exception_ptr >;

	//Apparently reduce needs a first parameter of a 'return' value for whatever reason
	auto reduce = [ this ]( [[maybe_unused]] uint64_t&, const Output& var ) -> void
	{
		++filesProcessed;

		try
		{
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
			else if ( auto valExcept = std::get_if< std::exception_ptr >( &var ); valExcept != nullptr )
			{
				std::rethrow_exception( *valExcept );
			}
			else
			{ throw std::runtime_error( "Unknown variant type" ); }
		}
		catch ( FileAlreadyExistsException& e )
		{
			++alreadyinDB;
			return;
		}
		catch ( FileDeletedException& e )
		{
			++deleted;
			return;
		}
		catch ( std::exception& e )
		{
			++failed;
			return;
		}

		return;
	};

	auto process = []( std::filesystem::path path_ ) -> Output
	{
		try
		{
			if ( !std::filesystem::exists( path_ ) )
			{
				spdlog::error( "File {} does not exist", path_.string() );
				throw std::runtime_error( "File does not exist" );
			}

			const std::vector< std::byte > bytes = [ &path_ ]() -> std::vector< std::byte >
			{
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

			// Generate filepath location from hash
			const std::filesystem::path filepath = getFilepath( sha256 );

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

				internal::raii_wrapper file(
					::open(
						filepath.c_str(), O_WRONLY | O_CREAT | O_SYNC, S_IRUSR | S_IWUSR
					), []( int i ) { ::close( i ); }
				);

				int ofs = file.object;

				if ( ofs == -1 )
				{
					spdlog::error( "Failed to open file {}", filepath.string() );

					std::exception_ptr eptr { std::make_exception_ptr(
						std::system_error( errno, std::generic_category() )
					) };
					return Output( eptr );
				}

				::write( ofs, bytes.data(), bytes.size() );

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
			try
			{
				hash_id = getFileID( sha256 );
			}
			catch ( ... )
			{

			}

			if ( hash_id )
			{
				//We got an ID. Throw FileExists
				throw FileAlreadyExistsException( "hash_id:" + std::to_string( hash_id ) );
			}

			hash_id = addFile( sha256 );
			
			return Output( hash_id );
		}
		catch ( ... )
		{
			auto eptr = std::current_exception();
			return Output( eptr );
		}
	};

	auto future = QtConcurrent::mappedReduced(
		&pool, files, process, reduce
	);

	while ( !future.isFinished() )
	{
		QThread::yieldCurrentThread();
		QThread::msleep( 250 );
		emit updateValues();
	}
}


void ImportViewer::updateValues_slot()
{
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