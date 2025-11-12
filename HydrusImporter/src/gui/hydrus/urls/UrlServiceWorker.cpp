//
// Created by kj16609 on 11/7/25.
//
#include "UrlServiceWorker.hpp"

#include <moc_UrlServiceWorker.cpp>

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
	idhan::hydrus::TransactionBaseCoro client_tr { m_importer->client_db };

	std::size_t url_counter { 0 };

	idhan::hydrus::Query< int, int > query { client_tr, "SELECT hash_id, url_id FROM url_map" };

	for ( [[maybe_unused]] const auto& [ hash_id, url_id ] : query )
	{
		url_counter += 1;
		if ( url_counter % 10'000 == 0 ) emit processedMaxUrls( url_counter );
	}

	emit processedMaxUrls( url_counter );
}

void UrlServiceWorker::process()
{
	auto& client { idhan::IDHANClient::instance() };

	idhan::hydrus::TransactionBaseCoro client_tr { m_importer->client_db };
	idhan::hydrus::TransactionBaseCoro master_tr { m_importer->master_db };

	std::size_t url_counter { 0 };

	idhan::hydrus::Query< int, int > query { client_tr, "SELECT hash_id, url_id FROM url_map ORDER BY hash_id ASC" };

	std::unordered_map< idhan::hydrus::HashID, std::vector< std::string > > current_urls {};

	auto flushUrls = [ &, this ]()
	{
		emit statusMessage( "Mapping hydrus IDs to IDHAN IDs" );
		std::vector< idhan::hydrus::HashID > hashes {};
		for ( const auto& hash_id : current_urls | std::views::keys )
		{
			hashes.emplace_back( hash_id );
		}

		const auto mapped_ids { m_importer->mapHydrusRecords( hashes ) };

		emit statusMessage( "Adding URLs to records" );

		std::vector< QFuture< void > > futures {};

		for ( const auto& [ hash_id, idhan_id ] : mapped_ids )
		{
			auto urls { current_urls[ hash_id ] };
			auto future { client.addUrls( idhan_id, urls ) };
			// futures.emplace_back( client.addUrls( idhan_id, urls ) );
			future.waitForFinished();
		}

		for ( auto& future : futures ) future.waitForFinished();

		current_urls.clear();
		emit processedUrls( url_counter );
	};

	for ( [[maybe_unused]] const auto& [ hash_id, url_id ] : query )
	{
		idhan::hydrus::Query< std::string_view > url_query {
			master_tr, "SELECT url FROM urls WHERE url_id = $1", url_id
		};

		std::vector< std::string > urls {};

		for ( const auto& [ url ] : url_query )
		{
			urls.emplace_back( url );
		}

		url_counter += urls.size();

		if ( auto itter = current_urls.find( hash_id ); itter != current_urls.end() )
			itter->second.insert( itter->second.end(), urls.begin(), urls.end() );
		else
			current_urls.emplace( hash_id, std::move( urls ) );

		if ( url_counter % 500 == 0 ) flushUrls();
	}

	flushUrls();

	emit statusMessage( "Finished!" );
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
