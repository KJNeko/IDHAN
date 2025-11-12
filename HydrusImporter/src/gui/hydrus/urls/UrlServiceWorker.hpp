//
// Created by kj16609 on 11/7/25.
//
#pragma once

#include <QObject>

#include "HydrusImporter.hpp"

class UrlServiceWorker : public QObject, public QRunnable
{
	Q_OBJECT

	idhan::hydrus::HydrusImporter* m_importer;
	bool m_preprocessed { false };

  signals:
	void processedMaxUrls( std::size_t counter );
	void processedUrls( std::size_t counter );
	void statusMessage( const QString& message );

  public:

	UrlServiceWorker( QObject* parent, idhan::hydrus::HydrusImporter* importer );
	void preprocess();
	void process();

	void run() override;
};
