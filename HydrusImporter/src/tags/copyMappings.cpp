//
// Created by kj16609 on 3/8/25.
//

#include <qtconcurrentrun.h>

#include "HydrusImporter.hpp"
#include "idhan/logging/logger.hpp"
#include "sqlitehelper/Transaction.hpp"

namespace idhan::hydrus
{

void HydrusImporter::copyMappings()
{
	logging::info( "Getting mappings" );

	TransactionBase client_tr { client_db };

	QThreadPool pool {};
	pool.setMaxThreadCount( 8 );

	QFutureSynchronizer< void > sync {};

	std::vector< std::pair< TagDomainID, std::size_t > > domains {};

	client_tr << "SELECT name, service_id FROM services WHERE service_type = 0 OR service_type = 5" >>
		[ & ]( const std::string name, const std::size_t service_id )
	{
		QFuture< TagDomainID > domain_id_future { m_client->getTagDomain( name ) };

		domain_id_future.waitForFinished();

		domains.emplace_back( domain_id_future.result(), service_id );
	};

	for ( const auto& [ domain_id, service_id ] : domains )
	{
		sync.addFuture( QtConcurrent::run( &pool, &HydrusImporter::copyDomainMappings, this, domain_id, service_id ) );
	}

	sync.waitForFinished();
}

void HydrusImporter::copyDomainMappings( const TagDomainID domain_id, const std::size_t hy_service_id )
{
	const auto table_name { std::format( "current_mappings_{}", hy_service_id ) };

	TransactionBase mappings_tr { mappings_db };
	TransactionBase inner_mappings_tr { mappings_db };
	TransactionBase master_tr { master_db };

	logging::info( "Copying mappings from {}", table_name );

	const auto inner_query { std::format( "SELECT hash_id FROM {} WHERE tag_id = $1", table_name ) };

	QFuture< void > future { QtFuture::makeReadyVoidFuture() };

	mappings_tr << std::format(
		"SELECT tag_id, count(tag_id) AS count FROM {} GROUP BY tag_id ORDER BY count DESC", table_name )
		>> [ & ]( const std::size_t tag_id, const std::size_t count )
	{
		// now to get the tags
		std::vector< std::pair< std::string, std::string > > tags {};

		master_tr << "SELECT namespace, subtag FROM tags NATURAL JOIN namespaces NATURAL JOIN subtags WHERE tag_id = $1"
				  << tag_id
			>> [ &tags ]( const std::string_view namespace_text, const std::string_view subtag_text )
		{ tags.emplace_back( namespace_text, subtag_text ); };

		std::vector< std::string > hashes {};

		inner_mappings_tr << inner_query << tag_id >>
			[ this, &future, &master_tr, &hashes, domain_id, &tags ]( const std::size_t hash_id )
		{
			// now to get the hash
			master_tr << "SELECT hex(hash) FROM hashes WHERE hash_id = $1" << hash_id >>
				[ this, &future, hash_id, &hashes, domain_id, &tags ]( const std::string_view sha256_hash )
			{
				if ( sha256_hash.empty() || sha256_hash.size() != 64 )
				{
					logging::warn( "Hash ID {} was corrupted! Size of : {}", hash_id, sha256_hash.size() );
					return;
				}

				hashes.emplace_back( sha256_hash );

				if ( hashes.size() > 1024 * 64 )
				{
					QFuture< std::vector< RecordID > > records_future { m_client->createRecords( hashes ) };

					records_future.waitForFinished();

					std::vector< RecordID > records { records_future.result() };

					future.waitForFinished();

					future = m_client->addTags( std::move( records ), domain_id, tags );
					records.clear();
				}
			};
		};

		QFuture< std::vector< RecordID > > records_future { m_client->createRecords( hashes ) };

		records_future.waitForFinished();

		std::vector< RecordID > records { records_future.result() };

		future.waitForFinished();

		future = m_client->addTags( std::move( records ), domain_id, std::move( tags ) );

		logging::info( "Copied mappings for tag {}:{} with {} records", tags[ 0 ].first, tags[ 0 ].second, count );
	};
}

} // namespace idhan::hydrus