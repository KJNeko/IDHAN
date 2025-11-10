//
// Created by kj16609 on 9/11/24.
//

#include "HydrusImporter.hpp"

#include <QtConcurrentRun>

#include "hydrus_constants.hpp"
#include "sqlitehelper/Query.hpp"
#include "sqlitehelper/Transaction.hpp"
#include "sqlitehelper/TransactionBaseCoro.hpp"

namespace idhan::hydrus
{

constexpr std::string_view LOCK_FILE_NAME { "client_running" };

HydrusImporter::HydrusImporter( const std::filesystem::path& path ) :
  m_path( path ),
  final_future(),
  m_process_ptr_mappings( false )
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
	sqlite3_close_v2( master_db );
	sqlite3_close_v2( client_db );
	sqlite3_close_v2( mappings_db );
}

std::unordered_map< HashID, RecordID > HydrusImporter::mapHydrusRecords( std::vector< HashID > hash_ids ) const
{
	idhan::hydrus::TransactionBase master_tr { master_db };
	if ( hash_ids.empty() ) return {};

	std::ranges::sort( hash_ids );
	std::ranges::unique( hash_ids );

	std::unordered_map< std::uint32_t, std::string > hashes_map {};
	hashes_map.reserve( hash_ids.size() );

	for ( const auto& hash_id : hash_ids )
	{
		master_tr << "SELECT hex(hash) FROM hashes WHERE hash_id = $1" << hash_id >>
			[ & ]( const std::string_view hash_i )
		{
			if ( hash_i.size() != ( 256 / 8 * 2 ) )
			{
				return;
			}
			hashes_map.emplace( hash_id, hash_i );
		};
	}

	std::vector< std::string > hashes {};
	hashes.reserve( hash_ids.size() );
	for ( const auto& hash : hashes_map | std::views::values ) hashes.emplace_back( hash );

	auto& client { idhan::IDHANClient::instance() };
	auto created_records { client.createRecords( hashes ) };

	created_records.waitForFinished();

	const auto created_records_result { created_records.result<>() };
	if ( created_records_result.size() != hashes.size() ) throw std::runtime_error( "Failed to create records" );

	std::unordered_map< std::string, RecordID > record_map {};

	for ( const auto& [ record_id, hash ] : std::ranges::views::zip( created_records_result, hashes ) )
	{
		record_map.emplace( hash, record_id );
	}

	std::unordered_map< HashID, RecordID > record_id_map {};

	record_id_map.reserve( hash_ids.size() );

	for ( const auto& [ hy_id, hash ] : hashes_map )
	{
		const auto record_id { record_map.at( hash ) };
		record_id_map.emplace( hy_id, record_id );
	}

	return record_id_map;
}

RecordID HydrusImporter::getRecordIDFromHyID( const HashID hash_id )
{
	idhan::hydrus::TransactionBaseCoro client_tr { client_db };
	idhan::hydrus::Query< std::string_view > query {
		client_tr, "SELECT hex(hash) FROM hashes WHERE hash_id = $1", hash_id
	};

	auto& client { IDHANClient::instance() };

	std::vector< std::string > hashes {};

	for ( const auto& [ hash ] : query )
	{
		hashes.emplace_back( hash );
	}

	auto future { client.getRecordID( hashes.front() ) };

	future.waitForFinished();

	const auto result { future.result<>() };
	if ( !result ) throw std::runtime_error( "Failed to get record from client" );

	return result.value();
}

bool HydrusImporter::hasPTR() const
{
	bool exists { false };

	TransactionBaseCoro client_tr { client_db };

	Query< std::size_t, std::string_view > query {
		client_tr,
		"SELECT service_id, name FROM services WHERE service_type = $1",
		static_cast< int >( hy_constants::ServiceTypes::PTR_SERVICE )
	};

	for ( [[maybe_unused]] const auto& [ serviae_id, name ] : query )
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