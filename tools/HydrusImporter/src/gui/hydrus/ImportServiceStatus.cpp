#include "ImportServiceStatus.hpp"

#include "ImportServiceLabel.hpp"

namespace idhan::hydrus
{

ImportServiceStatus::ImportServiceStatus(
	ImportServiceLabel* label,
	ImportServiceLabel* timing_label,
	QObject* parent ) :
  QObject( parent ),
  m_label( label ),
  m_timing_label( timing_label )
{
	m_label->setTextFormat( Qt::PlainText );
	m_label->setWordWrap( true );
	m_label->setText( "Waiting for preprocessing" );
	m_timing_label->setAlignment( Qt::AlignLeft | Qt::AlignBottom );
	m_timer.setInterval( 1000 );
	connect( &m_timer, &QTimer::timeout, this, &ImportServiceStatus::refresh );
}

void ImportServiceStatus::begin( const QString& operation )
{
	m_running = true;
	m_failed = false;
	m_has_progress = false;
	m_warnings = 0;
	m_last_warning.clear();
	m_progress_time.start();
	setOperation( operation );
	m_timer.start();
}

void ImportServiceStatus::setOperation( const QString& operation )
{
	if ( !m_running ) return;

	m_operation = operation;
	m_operation_time.start();
	refresh();
}

void ImportServiceStatus::recordProgress()
{
	if ( !m_running ) return;

	m_has_progress = true;
	m_progress_time.restart();
	refresh();
}

void ImportServiceStatus::warning( const QString& message )
{
	++m_warnings;
	m_last_warning = message;
	refresh();
}

void ImportServiceStatus::finish( const QString& message )
{
	if ( m_failed ) return;

	m_running = false;
	m_timer.stop();
	m_operation = message;
	refresh();
}

void ImportServiceStatus::fail( const QString& message )
{
	m_running = false;
	m_failed = true;
	m_timer.stop();
	m_operation = QString( "Failed while %1: %2" ).arg( m_operation, message );
	refresh();
}

void ImportServiceStatus::refresh()
{
	QString text { m_operation };
	QString timing;
	if ( m_running )
	{
		timing = QString( "Operation elapsed: %L1s | %2: %L3s" )
		             .arg( m_operation_time.elapsed() / 1000 )
		             .arg( m_has_progress ? "Since last completed batch" : "No batch completed yet; elapsed" )
		             .arg( m_progress_time.elapsed() / 1000 );
	}

	if ( m_warnings > 0 )
	{
		text += QString( "\nErrors: %L1 | Last error: %2" ).arg( m_warnings ).arg( m_last_warning );
	}

	m_label->setText( text );
	m_label->setToolTip( text );
	m_timing_label->setText( timing );
	m_timing_label->setToolTip( timing );
}

} // namespace idhan::hydrus
