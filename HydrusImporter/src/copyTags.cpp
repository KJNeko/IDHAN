//
// Created by kj16609 on 2/20/25.
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

void HydrusImporter::copyTags()
{
	logging::info( "Copying tags" );
	TransactionBase transaction { master_db };

	std::shared_ptr synchronizer { std::make_shared< QFutureSynchronizer< TagID > >() };

	constexpr std::size_t max_threads { 128 };
	const std::uint32_t thread_count {
		std::min( static_cast< std::uint32_t >( 8 ), std::thread::hardware_concurrency() )
	};

	std::counting_semaphore< max_threads > sync_counter { thread_count * 2 };

	QFutureSynchronizer< void > thread_sync {};

	constexpr std::size_t group_size { 1024 * 32 };

	using TagPairGroup = std::vector< std::pair< std::string, std::string > >;

	TagPairGroup tag_pairs {};
	tag_pairs.resize( group_size );

	const auto start { std::chrono::steady_clock::now() };
	auto last_start { start };
	std::atomic< std::size_t > processed { 0 };
	std::size_t total_processed { 0 };

	spdlog::info( "Getting tag count" );
	std::size_t tag_count { 0 };
	transaction << "SELECT count(*) FROM tags" >> tag_count;

	auto printProcessed = [ &processed, start, &last_start, &tag_count, &total_processed ]()
	{
		std::size_t processed_count { processed.exchange( 0 ) };
		total_processed += processed_count;

		if ( processed_count == 0 ) return;

		const auto end { std::chrono::steady_clock::now() };
		const auto total_duration { end - start };

		const std::chrono::duration< double > duration { end - last_start };

		last_start = end;

		const auto time_per_item { total_duration / total_processed };

		spdlog::info(
			"[tags]: {} ({:2.1f}%): Rate: {}/s, ETA: {}, (Total time: {})",
			total_processed,
			( static_cast< float >( total_processed ) / static_cast< float >( tag_count ) * 100.0f ),
			static_cast< std::size_t >( static_cast< double >( processed_count ) / duration.count() ),
			std::chrono::duration_cast< std::chrono::minutes >( time_per_item * ( tag_count - total_processed ) ),
			std::chrono::duration_cast< std::chrono::minutes >( total_duration ) );
	};

	QThreadPool pool {};
	pool.setMaxThreadCount( thread_count );

	QFuture< void > timer_future { QtConcurrent::
		                               run( &pool,
		                                    [ & ]( QPromise< void >& promise )
		                                    {
												QEventLoop loop {};
												QTimer m_timer {};
												m_timer.setInterval( 2000 );
												m_timer.setSingleShot( false );
												m_timer.callOnTimeout( printProcessed );
												m_timer.start();

												do {
													loop.processEvents();
													std::this_thread::yield();
													std::this_thread::sleep_for( std::chrono::microseconds( 100 ) );
												}
												while ( !promise.isCanceled() );

												return;
											} ) };

	std::size_t current_id { 0 };

	std::string current_namespace { "" };

	auto submitData = [ &processed, &sync_counter, &tag_pairs, &pool, &thread_sync ]()
	{
		thread_sync.addFuture(
			QtConcurrent::run(
				&pool,
				[ &processed, &sync_counter ]( TagPairGroup pairs )
				{
					QNetworkAccessManager network {};
					QEventLoop loop;

					QFutureWatcher< std::vector< TagID > > data_watcher {};
					data_watcher.setFuture( IDHANClient::instance().createTags( pairs ) );

					// create watcher to watch until the sync is finished
					QFutureWatcher< void > watcher {};
					QObject::connect( &watcher, &QFutureWatcher< void >::finished, &loop, &QEventLoop::quit );

					watcher.setFuture( QtConcurrent::run( [ & ]() { data_watcher.waitForFinished(); } ) );

					loop.exec();

					sync_counter.release();
					processed += pairs.size();
				},
				tag_pairs ) );
	};

	tag_pairs.resize( group_size );

	std::size_t max_count;

	transaction
			<< "SELECT DISTINCT count(*) FROM tags NATURAL JOIN namespaces NATURAL JOIN subtags ORDER BY length(namespace), namespace_id ASC"
		>> max_count;

	transaction
			<< "SELECT DISTINCT namespace, subtag FROM tags NATURAL JOIN namespaces NATURAL JOIN subtags ORDER BY length(namespace), namespace_id ASC"
		>> [ & ]( const std::string_view namespace_text, const std::string_view subtag_text )
	{
		if ( namespace_text != current_namespace )
		{
			current_namespace = namespace_text;
		}

		tag_pairs[ current_id ].first = namespace_text;
		tag_pairs[ current_id ].second = subtag_text;

		current_id++;

		if ( current_id >= group_size )
		{
			current_id = 0;

			sync_counter.acquire();

			submitData();
		}
	};

	tag_pairs.resize( current_id + 1 );

	submitData();

	thread_sync.addFuture( QtConcurrent::run( [ sync = std::move( synchronizer ) ]() { sync->waitForFinished(); } ) );
	thread_sync.waitForFinished();
	timer_future.cancel();

	printProcessed();

	logging::info( "Finished processing {} tags", total_processed );
}

} // namespace idhan::hydrus