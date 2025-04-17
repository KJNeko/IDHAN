//
// Created by kj16609 on 9/11/24.
//

#include "HydrusImporter.hpp"

#include <QCoreApplication>
#include <QFutureSynchronizer>
#include <QFutureWatcher>
#include <QtConcurrentRun>

#include "sqlitehelper/Transaction.hpp"

namespace idhan::hydrus
{

constexpr std::string_view LOCK_FILE_NAME { "client_running" };

HydrusImporter::HydrusImporter(
	const std::filesystem::path& path, std::shared_ptr< IDHANClient >& client, const bool process_ptr_flag ) :
  m_client( client ),
  m_path( path ),
  m_process_ptr_mappings( process_ptr_flag )
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

void HydrusImporter::copyFileInfo()
{
	copyFileStorage();
}

void HydrusImporter::copyHydrusInfo()
{
	// auto tags_processed { std::make_shared< std::atomic< bool > >( false ) };
	auto domains_processed { std::make_shared< std::atomic< bool > >( false ) };
	auto hashes_processed { std::make_shared< std::atomic< bool > >( false ) };

	QFuture< void > tag_domains { QtConcurrent::run(
		[ this, domains_processed ]()
		{
			this->copyTagDomains();
			domains_processed->store( true );
			domains_processed->notify_all();
		} ) };

	/*
	QFuture< void > tag_future { QtConcurrent::run(
		[ this, tags_processed ]()
		{
			// this->copyTags();
			tags_processed->store( true );
			tags_processed->notify_all();
		} ) };
	*/

	QFuture< void > parents_future { QtConcurrent::run(
		[ this, domains_processed ]
		{
			if ( !domains_processed->load() ) domains_processed->wait( false );
			// if ( !tags_processed->load() ) tags_processed->wait( false );
			this->copyParents();
		} ) };

	QFuture< void > aliases_future { QtConcurrent::run(
		[ this, domains_processed ]
		{
			if ( !domains_processed->load() ) domains_processed->wait( false );
			// if ( !tags_processed->load() ) tags_processed->wait( false );
			this->copySiblings();
		} ) };

	QFuture< void > hash_future { QtConcurrent::run(
		[ this, hashes_processed ]()
		{
			// this->copyHashes();
			hashes_processed->store( true );
			hashes_processed->notify_all();
		} ) };

	QFuture< void > mappings_future { QtConcurrent::run(
		[ hashes_processed, this, domains_processed ]()
		{
			if ( !domains_processed->load() ) domains_processed->wait( false );
			// if ( !tags_processed->load() ) tags_processed->wait( false );
			if ( !hashes_processed->load() ) hashes_processed->wait( false );

			this->copyMappings();
		} ) };

	// QFuture< void > url_future { hash_future.then( [ this ]() { this->copyUrls(); } ) };

	sync.addFuture( std::move( tag_domains ) );
	// sync.addFuture( std::move( tag_future ) );
	// sync.addFuture( std::move( parents_future ) );
	// sync.addFuture( std::move( aliases_future ) );
	sync.addFuture( std::move( hash_future ) );
	sync.addFuture( std::move( mappings_future ) );

	final_future = QtConcurrent::run( [ this ]() { sync.waitForFinished(); } ).then( [ this ]() { this->finish(); } );
}

} // namespace idhan::hydrus