//
// Created by kj16609 on 9/11/24.
//

#include "HydrusImporter.hpp"

#include <QCoreApplication>
#include <QFutureWatcher>
#include <QtConcurrentRun>

#include "hydrus_constants.hpp"
#include "sqlitehelper/Query.hpp"
#include "sqlitehelper/Transaction.hpp"
#include "sqlitehelper/TransactionBaseCoro.hpp"

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

bool HydrusImporter::hasPTR() const
{
	bool exists { false };

	TransactionBaseCoro client_tr { client_db };

	Query< std::size_t, std::string_view > query { client_tr,
		                                           "SELECT service_id, name FROM services WHERE service_type = $1",
		                                           static_cast< int >( hy_constants::ServiceTypes::PTR_SERVICE ) };

	for ( const auto& [ service_id, name ] : query )
	{
		exists = true;
	}

	return exists;
}

std::vector< ServiceInfo > HydrusImporter::getTagServices()
{
	TransactionBaseCoro client_tr { client_db };
	std::vector< ServiceInfo > services {};

	Query< std::size_t, std::string_view > query {
		client_tr,
		"SELECT service_id, name FROM services WHERE service_type = $1 OR service_type = $2",
		static_cast< int >( hy_constants::ServiceTypes::TAG_SERVICE ),
		static_cast< int >( hy_constants::ServiceTypes::PTR_SERVICE )
	};

	for ( const auto& [ service_id, name ] : query )
	{
		ServiceInfo info {};
		info.service_id = service_id;
		info.name = QString::fromStdString( std::string( name ) );
		services.emplace_back( std::move( info ) );
	}

	return services;
}

} // namespace idhan::hydrus