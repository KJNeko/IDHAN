//
// Created by kj16609 on 9/11/24.
//

#include "HydrusImporter.hpp"

#include <QCoreApplication>
#include <QFutureSynchronizer>
#include <QFutureWatcher>
#include <QtConcurrentRun>

#include "hydrus_constants.hpp"
#include "sqlitehelper/Transaction.hpp"

namespace idhan::hydrus
{

constexpr std::string_view LOCK_FILE_NAME { "client_running" };

HydrusImporter::HydrusImporter( const std::filesystem::path& path ) : m_path( path ), m_process_ptr_mappings( false )
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

	QFuture< void > files_future { QtConcurrent::run( [ this ]() { this->copyFileStorage(); } ) };

	// QFuture< void > url_future { hash_future.then( [ this ]() { this->copyUrls(); } ) };

	sync.addFuture( std::move( tag_domains ) );
	// sync.addFuture( std::move( tag_future ) );
	sync.addFuture( std::move( parents_future ) );
	sync.addFuture( std::move( aliases_future ) );
	sync.addFuture( std::move( hash_future ) );
	sync.addFuture( std::move( mappings_future ) );
	sync.addFuture( std::move( files_future ) );

	final_future = QtConcurrent::run( [ this ]() { sync.waitForFinished(); } ).then( [ this ]() { this->finish(); } );
}

bool HydrusImporter::hasPTR() const
{
	bool exists { false };

	TransactionBase client_tr { client_db };

	client_tr << "SELECT service_id, name FROM services WHERE service_type = $1"
			  << static_cast< int >( hy_constants::ServiceTypes::PTR_SERVICE )
		>> [ & ]( const std::size_t service_id, const std::string name ) { exists = true; };

	return exists;
}

std::vector< ServiceInfo > HydrusImporter::getTagServices()
{
	TransactionBase client_tr { client_db };
	std::vector< ServiceInfo > services {};

	client_tr << "SELECT service_id, name FROM services WHERE service_type = $1 OR service_type = $2"
			  << static_cast< int >( hy_constants::ServiceTypes::PTR_SERVICE )
			  << static_cast< int >( hy_constants::ServiceTypes::TAG_SERVICE )
		>> [ & ]( const std::size_t service_id, const std::string name )
	{
		ServiceInfo info {};
		info.service_id = service_id;
		info.name = QString::fromStdString( name );
		services.emplace_back( std::move( info ) );
	};

	// TransactionBase mappings_tr { mappings_db };

	/*
	for ( auto& service_info : services )
	{
		mappings_tr << std::format( "SELECT count(*) FROM current_mappings_{}", service_info.service_id )
			>> service_info.num_mappings;
		client_tr << std::format( "SELECT count(*) FROM current_tag_parents_{}", service_info.service_id )
			>> service_info.num_parents;
		client_tr << std::format( "SELECT count(*) FROM current_tag_siblings_{}", service_info.service_id )
			>> service_info.num_aliases;
	}
	*/

	return services;
}

} // namespace idhan::hydrus