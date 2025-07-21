//
// Created by kj16609 on 6/28/25.
//
#pragma once

#include <QSettings>
#include <QWidget>

#include <qthreadpool.h>

class TagServiceWorker;

namespace idhan::hydrus
{
class HydrusImporter;
}

namespace Ui
{
class HydrusImporterWidget;
}

class HydrusImporterWidget final : public QWidget
{
	Q_OBJECT

	QSettings settings { QSettings::IniFormat, QSettings::UserScope, "IDHAN", "IDHAN Importer" };
	std::unique_ptr< idhan::hydrus::HydrusImporter > m_importer { nullptr };

	QThreadPool m_threads;

  public:

	explicit HydrusImporterWidget( QWidget* parent = nullptr );
	~HydrusImporterWidget() override;

  public slots:
	void on_hydrusFolderPath_textChanged( const QString& path );
	void testHydrusPath();
	void on_selectHydrusPath_clicked();
	void on_parseHydrusDB_pressed();
	void on_importButton_pressed();

  signals:
	void triggerPreImport();
	void triggerImport();

  private:

	Ui::HydrusImporterWidget* ui;
};
