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
#include "sqlitehelper/Transaction.hpp"
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
		idhan::logging::error( "Error during URL preprocessing: {}", e.what() );
		emit errorOccurred( QString( "Error during preprocessing: %1" ).arg( e.what() ) );
	}

	emit finished();
}

void UrlServiceWorker::process()
{
	try
	{
		auto& client { idhan::IDHANClient::instance() };

		idhan::hydrus::TransactionBaseCoro client_tr { m_importer->client_db };

		std::size_t total_records_processed = 0;

		constexpr std::size_t URL_MAX = 500;

		emit statusMessage( "Processing URLs" );

		// Collect all unique url_ids (single query)
		std::vector< int > all_url_ids {};
		{
			idhan::hydrus::Query< int > url_ids_query {
				client_tr, "SELECT DISTINCT url_id FROM url_map ORDER BY url_id DESC"
			};
			for ( const auto& [ url_id ] : url_ids_query )
			{
				all_url_ids.push_back( url_id );
			}
		}

		// Process in chunks, using batched queries per chunk
		for ( std::size_t chunk_offset = 0; chunk_offset < all_url_ids.size(); chunk_offset += URL_MAX )
		{
			const auto chunk_end = std::min( chunk_offset + URL_MAX, all_url_ids.size() );
			const auto chunk_size = chunk_end - chunk_offset;

			std::unordered_map< idhan::hydrus::HashID, std::vector< std::string > > current_urls;

			// Batch query urls table: get url text for all url_ids in this chunk
			std::unordered_map< int, std::string > url_text_map {};
			{
				std::string sql = "SELECT url_id, url FROM urls WHERE url_id IN (";
				for ( std::size_t i = 0; i < chunk_size; ++i )
				{
					if ( i > 0 ) sql += ", ";
					sql += "?";
				}
				sql += ")";

				idhan::hydrus::TransactionBase master_tr { m_importer->master_db };
				auto binder = master_tr << sql;
				for ( std::size_t i = 0; i < chunk_size; ++i )
				{
					binder << all_url_ids[ chunk_offset + i ];
				}
				binder >> [ & ]( int url_id, std::string_view url )
				{ url_text_map.emplace( url_id, std::string( url ) ); };
			}

			// Batch query url_map: get hash_ids for all url_ids in this chunk
			std::unordered_map< int, std::vector< int > > url_hash_map {};
			{
				std::string sql = "SELECT url_id, hash_id FROM url_map WHERE url_id IN (";
				for ( std::size_t i = 0; i < chunk_size; ++i )
				{
					if ( i > 0 ) sql += ", ";
					sql += "?";
				}
				sql += ")";

				idhan::hydrus::TransactionBase client_tr_sync { m_importer->client_db };
				auto binder = client_tr_sync << sql;
				for ( std::size_t i = 0; i < chunk_size; ++i )
				{
					binder << all_url_ids[ chunk_offset + i ];
				}
				binder >> [ & ]( int url_id, int hash_id ) { url_hash_map[ url_id ].push_back( hash_id ); };
			}

			// Build current_urls map from batched data in memory
			std::size_t records_processed = 0;
			for ( const auto& [ url_id, hash_ids ] : url_hash_map )
			{
				const auto url_it = url_text_map.find( url_id );
				if ( url_it == url_text_map.end() ) continue;

				for ( const auto& hash_id : hash_ids )
				{
					current_urls[ hash_id ].emplace_back( url_it->second );
					++records_processed;
				}
			}

			// Flush this chunk to server
			if ( !current_urls.empty() )
			{
				total_records_processed += records_processed;
				flushUrls( current_urls, client, total_records_processed );
			}
		}

		emit statusMessage( "Finished!" );
	}
	catch ( const std::exception& e )
	{
		idhan::logging::error( "Error during URL processing: {}", e.what() );
		emit errorOccurred( QString( "Error during processing: %1" ).arg( e.what() ) );
	}
}

void UrlServiceWorker::flushUrls(
	std::unordered_map< idhan::hydrus::HashID, std::vector< std::string > >& current_urls,
	idhan::IDHANClient& client,
	std::size_t url_counter )
{
	if ( current_urls.empty() ) return;

	// Only map hashes we haven't already resolved on a previous chunk.
	std::vector< idhan::hydrus::HashID > uncached;
	uncached.reserve( current_urls.size() );

	for ( const auto& hash_id : current_urls | std::views::keys )
	{
		if ( !m_record_cache.contains( hash_id ) ) uncached.emplace_back( hash_id );
	}

	if ( !uncached.empty() )
	{
		const auto mapped_ids { m_importer->mapHydrusRecords( uncached ) };
		for ( const auto& [ hy_hash_id, idhan_id ] : mapped_ids )
			m_record_cache.insert_or_assign( hy_hash_id, idhan_id );
	}

	// Count actual unique URLs using a set
	std::unordered_set< std::string > unique_urls;
	for ( const auto& urls : current_urls | std::views::values )
	{
		for ( const auto& url : urls )
		{
			unique_urls.insert( url );
		}
	}

	emit statusMessage(
		QString( "Adding %1 URLs to %2 records" ).arg( unique_urls.size() ).arg( current_urls.size() ) );

	// Use QFutureSynchronizer for proper parallel processing
	QFutureSynchronizer< void > synchronizer;

	for ( auto& [ hash_id, urls ] : current_urls )
	{
		const auto itter { m_record_cache.find( hash_id ) };
		if ( itter == m_record_cache.end() ) continue; // hash could not be mapped to a record

		synchronizer.addFuture( client.addUrls( itter->second, std::move( urls ) ) );
	}

	synchronizer.waitForFinished();

	current_urls.clear();
	emit processedUrls( url_counter );
}

void UrlServiceWorker::run()
{
	try
	{
		if ( !m_preprocessed )
		{
			m_preprocessed = true;
			preprocess();
			return;
		}

		process();
	}
	catch ( std::exception& e )
	{
		idhan::logging::error( e.what() );
		emit errorOccurred( QString::fromStdString( e.what() ) );
	}
	catch ( ... )
	{
		idhan::logging::error( "Unknown exception in UrlServiceWorker" );
		emit errorOccurred( "Unknown exception during URL import" );
	}
}
