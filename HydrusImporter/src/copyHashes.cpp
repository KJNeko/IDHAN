//
// Created by kj16609 on 3/7/25.
//

#include <QEventLoop>
#include <QFutureSynchronizer>
#include <QFutureWatcher>
#include <QtConcurrentRun>

#include "HydrusImporter.hpp"
#include "idhan/logging/logger.hpp"
#include "sqlitehelper/Transaction.hpp"

namespace idhan::hydrus
{

void HydrusImporter::copyHashes()
{
	TransactionBase master_tr { master_db };

	std::vector< std::string > hashes {};

	QThreadPool pool {};

	std::uint32_t thread_count { 4 };

	pool.setMaxThreadCount( thread_count );

	std::counting_semaphore< 1024 > sync_counter { thread_count };
	QFutureSynchronizer< void > thread_sync {};

	auto submitData = [ & ]()
	{
		thread_sync.addFuture(
			QtConcurrent::
				run( &pool,
		             [ &sync_counter, client = m_client, data = std::move( hashes ) ]() -> void
		             {
						 QNetworkAccessManager network {};
						 QEventLoop loop {};

						 QFutureWatcher< std::vector< RecordID > > data_watcher {};
						 data_watcher.setFuture( client->createRecords( data, network ) );

						 QFutureWatcher< void > watcher {};
						 QObject::connect( &watcher, &QFutureWatcher< void >::finished, &loop, &QEventLoop::quit );

						 watcher.setFuture( QtConcurrent::run( [ & ]() { data_watcher.waitForFinished(); } ) );

						 loop.exec();

						 sync_counter.release();
					 } ) );
	};

	master_tr << "SELECT hash_id, hex(hash), hash FROM hashes" >>
		[ & ]( const std::uint64_t id, const std::string_view hex, std::vector< std::byte > data )
	{
		if ( hex.size() != 64 && data.size() != 32 )
		{
			logging::error( "hash_id {} had a corrupted hash of length {}!", id, hex.size() );
			return;
		}

		hashes.emplace_back( std::string( hex ) );

		if ( hashes.size() > 128 )
		{
			// dispatch to be added
			submitData();
		}
	};

	submitData();

	thread_sync.waitForFinished();
}

} // namespace idhan::hydrus
