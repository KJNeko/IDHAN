#include "ImportServiceLabel.hpp"

#include <QEvent>
#include <QResizeEvent>
#include <QShowEvent>

#include <algorithm>

namespace idhan::hydrus
{

ImportServiceLabel::ImportServiceLabel( QWidget* parent ) : QLabel( parent )
{
	setTextFormat( Qt::PlainText );
	setWordWrap( true );
	setAlignment( Qt::AlignLeft | Qt::AlignTop );
	QSizePolicy policy { QSizePolicy::Expanding, QSizePolicy::Minimum };
	policy.setHeightForWidth( true );
	setSizePolicy( policy );
	reserveHeight();
}

int ImportServiceLabel::minimumLines() const
{
	return m_minimum_lines;
}

void ImportServiceLabel::setMinimumLines( const int lines )
{
	m_minimum_lines = std::max( 1, lines );
	reserveHeight();
}

QSize ImportServiceLabel::sizeHint() const
{
	// Column widths are allocated by the layout, independently of the current message or counter values.
	return { 0, m_reserved_height };
}

QSize ImportServiceLabel::minimumSizeHint() const
{
	return sizeHint();
}

int ImportServiceLabel::heightForWidth( const int ) const
{
	// Measure wrapping at the actual width in reserveHeight(), rather than letting speculative layout
	// measurements change the scroll area's height whenever the text changes.
	return m_reserved_height;
}

void ImportServiceLabel::setText( const QString& text )
{
	if ( text == QLabel::text() ) return;

	QLabel::setText( text );
	reserveHeight();
}

void ImportServiceLabel::resizeEvent( QResizeEvent* event )
{
	QLabel::resizeEvent( event );
	reserveHeight();
}

void ImportServiceLabel::showEvent( QShowEvent* event )
{
	QLabel::showEvent( event );
	reserveHeight();
}

void ImportServiceLabel::changeEvent( QEvent* event )
{
	QLabel::changeEvent( event );
	if ( event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange ) reserveHeight();
}

void ImportServiceLabel::reserveHeight()
{
	const auto margins = contentsMargins();
	auto height = m_minimum_lines * fontMetrics().lineSpacing() + margins.top() + margins.bottom() + 2 * margin();
	if ( isVisible() && width() > 0 ) height = std::max( height, QLabel::heightForWidth( width() ) );
	if ( height <= m_reserved_height ) return;

	m_reserved_height = height;
	setMinimumHeight( height );
	updateGeometry();
}

} // namespace idhan::hydrus
