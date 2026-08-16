#pragma once

#include <QObject>

#include "HydrusImporter.hpp"

namespace idhan::hydrus
{
class TransactionBaseCoro;
}

//! QRunnable that imports Hydrus URL associations (each record's known URLs) into IDHAN.
class UrlServiceWorker : public QObject, public QRunnable
{
	Q_OBJECT

	idhan::hydrus::HydrusImporter* m_importer;
	bool m_preprocessed { false };

	//! Caches Hydrus-hash-id -> IDHAN-record-id across chunks so a record shared by several URLs is only mapped once.
	std::unordered_map< idhan::hydrus::HashID, idhan::RecordID > m_record_cache {};

  signals:
	void finished();
	void processedMaxUrls( std::size_t counter, std::size_t unique_counter );
	void processedUrls( std::size_t counter, std::size_t unique_counter );
	void statusMessage( const QString& message );
	void errorOccurred( const QString& message );

  public:

	UrlServiceWorker( QObject* parent, idhan::hydrus::HydrusImporter* importer );
	void preprocess();
	void process();

	void run() override;

  private:

	void flushUrls(
		std::unordered_map< idhan::hydrus::HashID, std::vector< std::string > >& current_urls,
		idhan::IDHANClient& client );
};
