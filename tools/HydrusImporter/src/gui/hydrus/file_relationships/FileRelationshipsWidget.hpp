//
// Created by kj16609 on 11/5/25.
//
#pragma once
#include "FileRelationshipsWorker.hpp"
#include "gui/hydrus/HydrusImporterWidget.hpp"

namespace Ui
{
class FileRelationshipsWidget;
}

class FileRelationshipsWidget : public QWidget
{
	idhan::hydrus::HydrusImporter* m_importer;

	std::size_t alternatives_processed { 0 };
	std::size_t alternatives_total { 0 };

	std::size_t duplicates_processed { 0 };
	std::size_t duplicates_total { 0 };
	FileRelationshipsWorker* m_worker;

  public:

	Q_DISABLE_COPY_MOVE( FileRelationshipsWidget );

	explicit FileRelationshipsWidget( idhan::hydrus::HydrusImporter* importer, QWidget* parent = nullptr );

  private:

	void updateText();

  public slots:
	void startImport();
	void startPreImport();
	void statusMessage( const QString& msg );

	void processedDuplicates( std::size_t count );
	void processedMaxDuplicates( std::size_t count );
	void processedAlternatives( std::size_t count );
	void processedMaxAlternatives( std::size_t count );

  private:

	Ui::FileRelationshipsWidget* ui;
};
