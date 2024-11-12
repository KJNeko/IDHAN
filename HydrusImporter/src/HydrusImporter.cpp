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
	std::counting_semaphore< 16 > sync_counter { 16 };

	QFutureSynchronizer< void > thread_sync;

	std::size_t counter { 0 };

	std::size_t pair_size { 0 };
	std::vector< std::pair< std::string, std::string > > tag_pairs {};

	transaction
			<< "SELECT namespace, subtag FROM tags NATURAL JOIN namespaces NATURAL JOIN subtags ORDER BY subtag_id ASC"
		>> [ & ]( const std::string_view namespace_text, const std::string_view subtag_text )
	{
		++counter;
		pair_size += namespace_text.size();
		pair_size += subtag_text.size();

		tag_pairs.emplace_back( std::make_pair( namespace_text, subtag_text ) );

		// 16KB assuming L1 cache for each core.
		if ( tag_pairs.size() > 50'000 )
		{
			pair_size = 0;
			sync_counter.acquire();
			thread_sync.addFuture( QtConcurrent::run(
				[ &sync_counter, client = m_client, data = std::move( tag_pairs ) ]()
				{
					QNetworkAccessManager network {};
					QEventLoop loop;
					QFutureSynchronizer< TagID > sync {};

					for ( const auto& [ nspace, stag ] : data )
						sync.addFuture( client->createTag( nspace, stag, network ) );

					// create watcher to watch until the sync is finished
					QFutureWatcher< void > watcher {};
					QObject::connect( &watcher, &QFutureWatcher< void >::finished, &loop, &QEventLoop::quit );

					watcher.setFuture( QtConcurrent::run( [ &sync ]() { sync.waitForFinished(); } ) );

					loop.exec();
					sync_counter.release();
				} ) );
		}

		if ( counter % 100'000 == 0 ) spdlog::info( "Imported {} tags so far", counter );
	};

	thread_sync.addFuture( QtConcurrent::run( [ sync = std::move( synchronizer ) ]() { sync->waitForFinished(); } ) );
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