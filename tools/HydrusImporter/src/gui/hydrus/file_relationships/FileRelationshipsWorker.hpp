//
// Created by kj16609 on 11/5/25.
//
#pragma once

#include <QObject>

#include "HydrusImporter.hpp"

class FileRelationshipsWorker : public QObject, public QRunnable
{
	Q_OBJECT

	idhan::hydrus::HydrusImporter* m_importer;
	bool m_preprocessed { false };

  signals:

	void processedMaxDuplicates( std::size_t counter );
	void processedMaxAlternatives( std::size_t counter );
	void statusMessage( const QString& message );
	void processedDuplicates( std::size_t );
	void processedAlternatives( std::size_t );

  public:

	FileRelationshipsWorker( QObject* parent, idhan::hydrus::HydrusImporter* importer );
	void preprocess();
	void process();

	void run() override;
};
