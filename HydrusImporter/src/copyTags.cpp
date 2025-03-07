//
// Created by kj16609 on 2/20/25.
//

#include <QCoreApplication>
#include <QEventLoop>
#include <QFutureSynchronizer>
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
	TransactionBase transaction { master_db };

	std::shared_ptr synchronizer { std::make_shared< QFutureSynchronizer< TagID > >() };

	constexpr std::size_t max_threads { 128 };
	const std::size_t thread_count { 6 };

	std::counting_semaphore< max_threads > sync_counter { thread_count };

	QFutureSynchronizer< void > thread_sync {};

	constexpr std::size_t group_size { 1024 * 8 };

	using TagPairGroup = std::vector< std::pair< std::string, std::string > >;

	std::mutex mtx {};
	std::queue< TagPairGroup > group_queue {};
	for ( std::size_t i = 0; i < thread_count + 1; ++i )
	{
		group_queue.emplace().resize( group_size );
	}

	auto getGroup = [ &mtx, &group_queue ]
	{
		std::lock_guard guard { mtx };
		auto group { std::move( group_queue.front() ) };
		group_queue.pop();
		return group;
	};

	TagPairGroup tag_pairs { getGroup() };

	const auto start { std::chrono::steady_clock::now() };
	std::atomic< std::size_t > processed { 0 };

	spdlog::info( "Getting tag count" );
	std::size_t tag_count { 0 };
	transaction << "SELECT count(*) FROM tags" >> tag_count;
	spdlog::info( "Tag count: {}", tag_count );

	auto printProcessed = [ &processed, &start, &tag_count ]()
	{
		const auto end { std::chrono::steady_clock::now() };
		const auto duration { end - start };

		std::size_t processed_count { processed };

		spdlog::info(
			"Processed {} tags since start: {}",
			processed_count,
			std::chrono::duration_cast< std::chrono::seconds >( duration ) );

		const std::chrono::duration< double > s_duration { end - start };

		const auto time_per_item { s_duration / processed_count };

		spdlog::info(
			"Rate: {}/s, ETA: {}, (Total time: {})",
			static_cast< std::size_t >( static_cast< double >( processed_count ) / s_duration.count() ),
			std::chrono::duration_cast< std::chrono::minutes >( time_per_item * ( tag_count - processed_count ) ),
			std::chrono::duration_cast< std::chrono::minutes >( time_per_item * tag_count ) );
	};

	QTimer m_timer {};
	m_timer.setInterval( 3000 );
	m_timer.setSingleShot( false );
	m_timer.callOnTimeout( printProcessed );
	m_timer.start();

	std::size_t current_id { 0 };

	QThreadPool pool {};
	pool.setMaxThreadCount( thread_count );

	std::string current_namespace {};

	auto submitData = [ & ]()
	{
		thread_sync.addFuture(
			QtConcurrent::
				run( &pool,
		             [ &printProcessed,
		               &processed,
		               group_size,
		               &sync_counter,
		               client = m_client,
		               data = std::move( tag_pairs ),
		               &mtx,
		               &group_queue ]()
		             {
						 QNetworkAccessManager network {};
						 QEventLoop loop;

						 QFutureWatcher< std::vector< TagID > > data_watcher {};
						 data_watcher.setFuture( client->createTags( data, network ) );

						 // create watcher to watch until the sync is finished
						 QFutureWatcher< void > watcher {};
						 QObject::connect( &watcher, &QFutureWatcher< void >::finished, &loop, &QEventLoop::quit );

						 watcher.setFuture( QtConcurrent::run( [ & ]() { data_watcher.waitForFinished(); } ) );

						 loop.exec();
						 {
							 std::lock_guard guard { mtx };
							 group_queue.push( std::move( data ) );
						 }
						 sync_counter.release();
						 processed += group_size;
					 } ) );
	};

	transaction
			<< "SELECT DISTINCT namespace, subtag FROM tags NATURAL JOIN namespaces NATURAL JOIN subtags ORDER BY namespace_id, length(namespace) ASC"
		>> [ & ]( const std::string_view namespace_text, const std::string_view subtag_text )
	{
		if ( namespace_text != current_namespace )
		{
			logging::debug( "Processing namespace: {}", namespace_text );
			current_namespace = namespace_text;
		}
		tag_pairs[ current_id ].first = namespace_text;
		tag_pairs[ current_id ].second = subtag_text;

		current_id++;

		if ( current_id >= group_size )
		{
			current_id = 0;

			if ( QThread::isMainThread() )
			{
				do {
					QCoreApplication::processEvents();
				}
				while ( !sync_counter.try_acquire_for( std::chrono::milliseconds( 250 ) ) );
			}
			else
			{
				sync_counter.acquire();
			}

			submitData();

			tag_pairs = getGroup();
		}
	};

	submitData();

	thread_sync.addFuture( QtConcurrent::run( [ sync = std::move( synchronizer ) ]() { sync->waitForFinished(); } ) );
	thread_sync.waitForFinished();
	m_timer.stop();

	printProcessed();
}

void HydrusImporter::copyParents()
{}

} // namespace idhan::hydrus