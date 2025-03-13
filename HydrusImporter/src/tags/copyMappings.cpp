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
	pool.setMaxThreadCount( 0 );

	QFutureSynchronizer< void > sync {};

	client_tr << "SELECT name, service_id FROM services WHERE service_type = 0 OR service_type = 5" >>
		[ & ]( const std::string name, const std::size_t service_id )
	{
		QFuture< TagDomainID > domain_id_future { m_client->getTagDomain( name ) };

		domain_id_future.waitForFinished();

		sync.addFuture(
			QtConcurrent::
				run( &pool, &HydrusImporter::copyDomainMappings, this, domain_id_future.result(), service_id ) );
	};

	sync.waitForFinished();
}

void HydrusImporter::copyDomainMappings( const TagDomainID domain_id, const std::size_t hy_service_id )
{
	const auto table_name { std::format( "current_mappings_{}", hy_service_id ) };

	TransactionBase mappings_tr { mappings_db };
	TransactionBase master_tr { master_db };

	logging::info( "Copying mappings from {}", table_name );

	master_tr << "SELECT hex(hash), hash_id FROM hashes" >>
		[ &mappings_tr, this, &table_name, domain_id ]( const std::string hash, const std::size_t hash_id )
	{
		if ( hash.size() != ( ( 256 / 8 ) * 2 ) )
		{
			logging::error( "hash_id {} had a corrupted hash of length {}!", hash_id, hash.size() );
			return;
		}

		std::vector< std::pair< std::string, std::string > > tags {};

		mappings_tr << std::format( "SELECT tag_id FROM {} WHERE hash_id = $1", table_name ) << hash_id >>
			[ this, &tags ]( const std::size_t tag_id )
		{
			TransactionBase inner_master_tr { master_db };

			inner_master_tr
					<< "SELECT namespace, subtag FROM tags NATURAL JOIN namespaces NATURAL JOIN subtags WHERE tag_id = $1"
					<< tag_id
				>> [ &tags ]( std::string namespace_text, std::string subtag_text )
			{ tags.emplace_back( namespace_text, subtag_text ); };
		};

		if ( tags.size() == 0 ) return;

		auto record_future { m_client->getRecordID( hash ) };

		record_future.waitForFinished();

		if ( record_future.resultCount() != 1 )
		{
			logging::error( "hash_id {} has {} record results", hash_id, record_future.resultCount() );
		}

		const RecordID record_id { record_future.result().value_or( 0 ) };

		if ( record_id == 0 )
		{
			logging::error( "hash id {}: {} has no record in IDHAN!", hash_id, hash );

			return;
		}

		auto future { m_client->addTags( record_id, domain_id, std::move( tags ) ) };

		future.waitForFinished();

		logging::info( "Processed {} mappings for hash {} from table {}", tags.size(), hash_id, table_name );
	};
	logging::debug( "Finished processing all hashes" );
}

} // namespace idhan::hydrus