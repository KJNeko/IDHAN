#include "PTRFlattenWorker.hpp"

#include <QLocale>

#include <spdlog/spdlog.h>

#include <utility>

#include "ptr/flatten/RunFlatten.hpp"

namespace idhan::hydrus::ptr
{

PTRFlattenWorker::PTRFlattenWorker( std::filesystem::path ptr_directory,
                                    std::filesystem::path output_directory,
	const bool discard_terminal_deletes,
	QObject* parent ) :
  QObject( parent ),
  QRunnable(),
  m_ptr_directory( std::move( ptr_directory ) ),
  m_output_directory( std::move( output_directory ) ),
  m_discard_terminal_deletes( discard_terminal_deletes )
{
	setAutoDelete( false );

	qRegisterMetaType< FlattenLiveStats >( "idhan::hydrus::ptr::FlattenLiveStats" );
}

PTRFlattenWorker::~PTRFlattenWorker() = default;

void PTRFlattenWorker::run()
{
	spdlog::info( "PTR flatten worker started: {} -> {}", m_ptr_directory.string(), m_output_directory.string() );

	FlattenCallbacks callbacks {};

	callbacks.cancelled = [ this ] { return m_cancelled.load(); };

	callbacks.stage = [ this ]( const std::string_view text )
	{ emit progress( QString::fromUtf8( text.data(), static_cast< qsizetype >( text.size() ) ) ); };

	callbacks.progress = [ this ]( const std::size_t done, const std::size_t total, const std::string_view text )
	{
		emit subProgress(
			static_cast< int >( done ),
			static_cast< int >( total ),
			QString( "%1 (%2 / %3)" )
				.arg( QString::fromUtf8( text.data(), static_cast< qsizetype >( text.size() ) ) )
				.arg( QLocale::system().toString( static_cast< qlonglong >( done ) ) )
				.arg( QLocale::system().toString( static_cast< qlonglong >( total ) ) ) );
	};

	callbacks.statsUpdated = [ this ]( const FlattenLiveStats& stats ) { emit statsUpdated( stats ); };

	const auto outcome = runFlatten(
		m_ptr_directory,
		m_output_directory,
		callbacks,
		FlattenOptions { .discard_terminal_deletes = m_discard_terminal_deletes } );

	if ( outcome.cancelled )
	{
		emit finished( false, "Cancelled" );
		return;
	}

	emit finished( outcome.success, QString::fromStdString( outcome.message ) );
}

} // namespace idhan::hydrus::ptr
