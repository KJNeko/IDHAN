//
// Created by kj16609 on 3/7/25.
//

#include <QCoreApplication>
#include <QEventLoop>
#include <QFutureWatcher>
#include <QTimer>
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

	const std::uint32_t thread_count { std::thread::hardware_concurrency() };

	pool.setMaxThreadCount( thread_count );

	std::counting_semaphore< 1024 > sync_counter { thread_count * 2 };
	QFutureSynchronizer< void > thread_sync {};

	std::uint32_t group_size { 1000 * 32 };
	std::atomic< std::size_t > processed { 0 };
	std::size_t total_processed { 0 };

	auto submitData = [ &thread_sync, &pool, &sync_counter, this, &hashes, &processed, group_size ]()
	{
		thread_sync.addFuture(
			QtConcurrent::run(
				&pool,
				[ &sync_counter, &processed, group_size, client = m_client ]( auto data ) -> void
				{
					QEventLoop loop {};

					QFutureWatcher< std::vector< RecordID > > data_watcher {};
					data_watcher.setFuture( client->createRecords( data ) );

					QFutureWatcher< void > watcher {};
					QObject::connect( &watcher, &QFutureWatcher< void >::finished, &loop, &QEventLoop::quit );

					watcher.setFuture( QtConcurrent::run( [ & ]() { data_watcher.waitForFinished(); } ) );

					processed += group_size;

					loop.exec();

					sync_counter.release();
				},
				hashes ) );

		hashes.clear();
		hashes.reserve( group_size );
	};

	const auto start { std::chrono::steady_clock::now() };
	auto last_start { start };
	std::size_t total_to_process { 0 };

	master_tr << "SELECT COUNT(*) FROM hashes" >> total_to_process;

	auto printProgress = [ &processed, start, &total_processed, &last_start, total_to_process ]()
	{
		const auto end { std::chrono::steady_clock::now() };
		const auto total_duration { end - start };
		const std::chrono::duration< double > duration { end - last_start };
		last_start = end;

		const std::size_t processed_count { processed.exchange( 0 ) };
		total_processed += processed_count;

		if ( processed_count == 0 ) return;

		const auto time_per_item { total_duration / total_processed };

		spdlog::info(
			"[hashes]: {} ({:2.1f}%): Rate: {} hashes/s, ETA: {}, (Total time: {})",
			total_processed,
			( static_cast< float >( total_processed ) / static_cast< float >( total_to_process ) * 100.0f ),
			static_cast< std::size_t >( static_cast< double >( processed_count ) / duration.count() ),
			std::chrono::duration_cast<
				std::chrono::minutes >( time_per_item * ( total_to_process - total_processed ) ),
			std::chrono::duration_cast< std::chrono::minutes >( total_duration ) );
	};

	QFuture< void > timer_future { QtConcurrent::
		                               run( &pool,
		                                    [ & ]( QPromise< void >& promise )
		                                    {
												QEventLoop loop {};
												QTimer m_timer {};
												m_timer.setInterval( 2000 );
												m_timer.setSingleShot( false );
												m_timer.callOnTimeout( printProgress );
												m_timer.start();

												do {
													loop.processEvents();
													std::this_thread::yield();
													std::this_thread::sleep_for( std::chrono::microseconds( 100 ) );
												}
												while ( !promise.isCanceled() );

												return;
											} ) };

	master_tr << "SELECT hash_id, hex(hash), hash FROM hashes" >>
		[ & ]( const std::uint64_t id, const std::string_view hex, std::vector< std::byte > data )
	{
		if ( hex.size() != 64 && data.size() != 32 )
		{
			logging::error( "hash_id {} had a corrupted hash of length {}!", id, hex.size() );
			return;
		}

		hashes.emplace_back( std::string( hex ) );

		if ( hashes.size() > group_size )
		{
			sync_counter.acquire();

			submitData();
		}
	};

	timer_future.cancel();

	submitData();

	thread_sync.waitForFinished();

	logging::info( "Finished importing {} hashes", total_processed );
}

} // namespace idhan::hydrus
