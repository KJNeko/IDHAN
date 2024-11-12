//
// Created by kj16609 on 9/11/24.
//

#include "HydrusImporter.hpp"

#include <QCoreApplication>
#include <QFutureSynchronizer>
#include <QFutureWatcher>
#include <QtConcurrentRun>

#include <queue>

#include "IDHANTypes.hpp"
#include "spdlog/spdlog.h"
#include "sqlitehelper/Transaction.hpp"

namespace idhan::hydrus
{

constexpr std::string_view LOCK_FILE_NAME { "client_running" };

static std::size_t core_count { std::thread::hardware_concurrency() };

void HydrusImporter::copyTags()
{
	TransactionBase transaction { master_db };

	std::shared_ptr synchronizer { std::make_shared< QFutureSynchronizer< TagID > >() };

	constexpr std::size_t max_threads { 128 };
	const std::size_t thread_count { std::thread::hardware_concurrency() };

	std::counting_semaphore< max_threads > sync_counter { std::thread::hardware_concurrency() };

	QFutureSynchronizer< void > thread_sync {};

	constexpr std::size_t group_size { 4'000 };

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

		std::size_t value { processed };
		spdlog::info(
			"Processed {} tags since start: {}",
			value,
			std::chrono::duration_cast< std::chrono::seconds >( duration ) );

		const std::chrono::duration< double > s_duration { end - start };

		const auto time_per_item { s_duration / value };

		spdlog::info(
			"Rate: {}/s, ETA: {}, (Total time: {})",
			static_cast< std::size_t >( static_cast< double >( value ) / s_duration.count() ),
			std::chrono::duration_cast< std::chrono::minutes >( time_per_item * ( tag_count - value ) ),
			std::chrono::duration_cast< std::chrono::minutes >( time_per_item * tag_count ) );
	};

	std::size_t current_id { 0 };

	QThreadPool pool {};
	pool.setMaxThreadCount( thread_count );

	transaction
			<< "SELECT namespace, subtag FROM tags NATURAL JOIN namespaces NATURAL JOIN subtags ORDER BY subtag_id ASC"
		>> [ & ]( const std::string_view namespace_text, const std::string_view subtag_text )
	{
		tag_pairs[ current_id ].first = namespace_text;
		tag_pairs[ current_id ].second = subtag_text;
		current_id++;

		if ( current_id >= group_size )
		{
			current_id = 0;
			sync_counter.acquire();

			thread_sync.addFuture( QtConcurrent::run(
				&pool,
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
					printProcessed();
				} ) );

			tag_pairs = getGroup();
		}
	};

	thread_sync.addFuture( QtConcurrent::run( [ sync = std::move( synchronizer ) ]() { sync->waitForFinished(); } ) );
	thread_sync.waitForFinished();
}

HydrusImporter::HydrusImporter( const std::filesystem::path& path, std::shared_ptr< IDHANClient >& client ) :
  m_client( client )
{
	if ( !std::filesystem::exists( path ) ) throw std::runtime_error( "Failed to open path to hydrus db" );

	const auto master_path { path / "client.master.db" };
	const auto client_path { path / "client.db" };
	const auto mappings_path { path / "client.mappings.db" };

	//Check that the dbs we want exists
	if ( !std::filesystem::exists( master_path ) ) throw std::runtime_error( "Failed to find client.master.db" );

	if ( !std::filesystem::exists( client_path ) ) throw std::runtime_error( "Failed to find client.db" );

	if ( !std::filesystem::exists( mappings_path ) ) throw std::runtime_error( "Failed to find client.mappings.db" );

	// We should write a more sophisticated test here.
	if ( std::filesystem::exists( path / LOCK_FILE_NAME ) )
		throw std::runtime_error( "Client detected as running. Aborting" );

	sqlite3_open_v2( master_path.c_str(), &master_db, SQLITE_OPEN_READONLY, nullptr );
	sqlite3_open_v2( client_path.c_str(), &client_db, SQLITE_OPEN_READONLY, nullptr );
	sqlite3_open_v2( mappings_path.c_str(), &mappings_db, SQLITE_OPEN_READONLY, nullptr );
}

HydrusImporter::~HydrusImporter()
{
	//TODO: Cleanup after ourselves

	sqlite3_close_v2( master_db );
	sqlite3_close_v2( client_db );
	sqlite3_close_v2( mappings_db );
}

} // namespace idhan::hydrus