//
// Created by kj16609 on 11/7/25.
//
#ifndef IDHAN_URLSERVICEWIDGET_HPP
#define IDHAN_URLSERVICEWIDGET_HPP

#include <QWidget>

class UrlServiceWorker;

namespace idhan::hydrus
{
class HydrusImporter;
}

QT_BEGIN_NAMESPACE

namespace Ui
{
class UrlServiceWidget;
}

QT_END_NAMESPACE

class UrlServiceWidget : public QWidget
{
	Q_OBJECT

	Ui::UrlServiceWidget* ui;
	idhan::hydrus::HydrusImporter* m_importer;
	UrlServiceWorker* m_worker { nullptr };
	std::size_t m_max_urls { 0 };

  public:

	explicit UrlServiceWidget( idhan::hydrus::HydrusImporter* get, QWidget* parent = nullptr );
	~UrlServiceWidget() override;

  public slots:
	void startPreImport();
	void startImport();
	void statusMessage( const QString& msg );
	void processedMaxUrls( std::size_t count );
	void processedUrls( std::size_t count );
};

#endif //IDHAN_URLSERVICEWIDGET_HPP
