//
// Created by kj16609 on 11/7/25.
//
#include "UrlServiceWorker.hpp"

#include <moc_UrlServiceWorker.cpp>

#include <QFutureSynchronizer>

#include <IDHAN>
#include <optional>
#include <ranges>
#include <unordered_set>

#include "sqlitehelper/Query.hpp"
#include "sqlitehelper/TransactionBaseCoro.hpp"

UrlServiceWorker::UrlServiceWorker( QObject* parent, idhan::hydrus::HydrusImporter* importer ) :
  QObject( parent ),
  QRunnable(),
  m_importer( importer )
{
	this->setAutoDelete( false );
}

void UrlServiceWorker::preprocess()
{
	try
	{
		idhan::hydrus::TransactionBaseCoro client_tr { m_importer->client_db };

		std::size_t url_counter { 0 };

		// Use COUNT for more efficient counting
		idhan::hydrus::Query< std::size_t > count_query { client_tr, "SELECT COUNT(*) FROM url_map" };

		for ( const auto& [ count ] : count_query )
		{
			url_counter = count;
			break;
		}

		emit processedMaxUrls( url_counter );
	}
	catch ( const std::exception& e )
	{
		emit statusMessage( QString( "Error during preprocessing: %1" ).arg( e.what() ) );
	}
}

void UrlServiceWorker::process()
{
	try
	{
		auto& client { idhan::IDHANClient::instance() };

		idhan::hydrus::TransactionBaseCoro client_tr { m_importer->client_db };
		idhan::hydrus::TransactionBaseCoro master_tr { m_importer->master_db };

		std::size_t url_counter { 0 };
		std::size_t total_records_processed = 0;

		constexpr std::size_t URL_MAX = 500; // Max URLs to scan per burst

		emit statusMessage( "Processing URLs" );

		std::unordered_map< idhan::hydrus::HashID, std::vector< std::string > > current_urls;
		std::size_t urls_scanned = 0;
		std::size_t records_processed = 0;

		idhan::hydrus::Query< int > url_ids_query {
			client_tr, "SELECT DISTINCT url_id FROM url_map ORDER BY url_id DESC"
		};

		for ( const auto& [ url_id ] : url_ids_query )
		{
			idhan::hydrus::Query< std::string_view > url_query {
				master_tr, "SELECT url FROM urls WHERE url_id = ?", url_id
			};

			std::string url;
			bool url_found = false;
			for ( const auto& [ url_str ] : url_query )
			{
				url = url_str;
				url_found = true;
				break;
			}

			if ( !url_found ) continue;

			idhan::hydrus::Query< int > records_map {
				client_tr, "SELECT hash_id FROM url_map WHERE url_id = ?", url_id
			};

			std::vector< int > hash_ids {};
			hash_ids.reserve( URL_MAX );
			for ( const auto& [ hash_id ] : records_map )
			{
				hash_ids.push_back( hash_id );
			}

			// Add this URL to all associated hashes
			for ( int hash_id : hash_ids )
			{
				current_urls[ hash_id ].emplace_back( url );
				++records_processed;
			}

			++urls_scanned;

			// Check if we've hit either limit
			if ( urls_scanned >= URL_MAX )
			{
				// Flush the URLs we've collected
				if ( !current_urls.empty() )
				{
					total_records_processed += records_processed;
					flushUrls( current_urls, client, total_records_processed );
				}

				// Reset counters for next batch
				urls_scanned = 0;
				records_processed = 0;
			}
		}

		// Flush any remaining URLs
		if ( !current_urls.empty() )
		{
			total_records_processed += records_processed;
			flushUrls( current_urls, client, total_records_processed );
		}

		emit statusMessage( "Finished!" );
	}

	catch ( const std::exception& e )
	{
		emit statusMessage( QString( "Error during processing: %1" ).arg( e.what() ) );
	}
}

void UrlServiceWorker::flushUrls(
	std::unordered_map< idhan::hydrus::HashID, std::vector< std::string > >& current_urls,
	idhan::IDHANClient& client,
	std::size_t url_counter )
{
	if ( current_urls.empty() ) return;

	std::vector< idhan::hydrus::HashID > hashes;
	hashes.reserve( current_urls.size() );

	for ( const auto& hash_id : current_urls | std::views::keys )
	{
		hashes.emplace_back( hash_id );
	}

	const auto mapped_ids { m_importer->mapHydrusRecords( hashes ) };

	// Count actual unique URLs using a set
	std::unordered_set< std::string > unique_urls;
	for ( const auto& [ hash_id, urls ] : current_urls )
	{
		for ( const auto& url : urls )
		{
			unique_urls.insert( url );
		}
	}

	emit statusMessage(
		QString( "Adding %2 unique URLs to %1 mappings" ).arg( mapped_ids.size() ).arg( unique_urls.size() ) );

	// Use QFutureSynchronizer for proper parallel processing
	QFutureSynchronizer< void > synchronizer;

	for ( const auto& [ hash_id, idhan_id ] : mapped_ids )
	{
		auto urls { std::move( current_urls[ hash_id ] ) };
		synchronizer.addFuture( client.addUrls( idhan_id, std::move( urls ) ) );
	}

	synchronizer.waitForFinished();

	current_urls.clear();
	emit processedUrls( url_counter );
}

void UrlServiceWorker::run()
{
	if ( !m_preprocessed )
	{
		m_preprocessed = true;
		preprocess();
		return;
	}

	process();
}
