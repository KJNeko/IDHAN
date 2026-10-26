#pragma once

#include <QLabel>

namespace idhan::hydrus
{

// Keep space for wrapped text once it has been displayed so processing updates do not move other services.
class ImportServiceLabel final : public QLabel
{
	Q_OBJECT
	Q_PROPERTY( int minimumLines READ minimumLines WRITE setMinimumLines )

	int m_minimum_lines { 1 };
	int m_reserved_height { 0 };

  public:

	Q_DISABLE_COPY_MOVE( ImportServiceLabel )

	explicit ImportServiceLabel( QWidget* parent = nullptr );

	int minimumLines() const;
	void setMinimumLines( int lines );
	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;
	int heightForWidth( int width ) const override;

  public slots:

	void setText( const QString& text );

  protected:

	void resizeEvent( QResizeEvent* event ) override;
	void showEvent( QShowEvent* event ) override;
	void changeEvent( QEvent* event ) override;

  private:

	void reserveHeight();
};

} // namespace idhan::hydrus
