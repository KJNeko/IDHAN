//
// Created by kj16609 on 3/8/25.
//

#include <QtConcurrentRun>

#include <ranges>
#include <semaphore>

#include "HydrusImporter.hpp"
#include "hydrus_constants.hpp"
#include "idhan/logging/logger.hpp"
#include "sqlitehelper/Transaction.hpp"

namespace idhan::hydrus
{

void HydrusImporter::copyMappings()
{
	logging::info( "Getting mappings" );

	TransactionBase client_tr { client_db };

	QThreadPool pool {};
	pool.setMaxThreadCount( 8 );

	QFutureSynchronizer< void > sync {};

	std::vector< std::pair< TagDomainID, std::size_t > > domains {};

	client_tr << "SELECT name, service_id, service_type FROM services WHERE service_type = 0 OR service_type = 5" >>
		[ & ]( const std::string_view name, const std::size_t service_id, const std::size_t service_type )
	{
		QFuture< TagDomainID > domain_id_future { IDHANClient::instance().getTagDomain( name ) };

		if ( !m_process_ptr_mappings && service_type == hy_constants::ServiceTypes::PTR_SERVICE )
		{
			// if the current table is for the ptr, and we are not told to process the ptr mappings, then skip this
			return;
		}

		domain_id_future.waitForFinished();

		domains.emplace_back( domain_id_future.result(), service_id );
	};

	for ( const auto& [ domain_id, service_id ] : domains )
	{
		// copyDomainMappings( domain_id, service_id );
		sync.addFuture( QtConcurrent::run( &pool, &HydrusImporter::copyDomainMappings, this, domain_id, service_id ) );
	}

	sync.waitForFinished();

	logging::info( "Finished processing all mappings" );
}

void HydrusImporter::finish()
{
	logging::info( "Finished processing Hydrus database into IDHAN" );
	const auto now { std::chrono::steady_clock::now() };

	const auto printTime = []( const auto time ) -> void { logging::info( "Total time took: {}", time ); };

	const auto time_elapsed { now - start_time };

	using namespace std::chrono_literals;

	const auto hours { std::chrono::duration_cast< std::chrono::hours >( time_elapsed ).count() };
	const auto minutes {
		std::chrono::duration_cast< std::chrono::minutes >( time_elapsed % std::chrono::hours( 1 ) ).count()
	};
	const auto seconds {
		std::chrono::duration_cast< std::chrono::seconds >( time_elapsed % std::chrono::minutes( 1 ) ).count()
	};

	std::string time_str {};
	if ( hours > 0 ) time_str += std::format( "{}h:", hours );
	if ( minutes > 0 ) time_str += std::format( "{}m:", minutes );
	time_str += std::format( "{}s", seconds );

	logging::info( "Total time took: {}", time_str );

	QCoreApplication::exit();
}

struct Set
{
	RecordID id;
	std::vector< TagID > tags;

	Set() = delete;

	Set( const RecordID record, std::vector< TagID >&& move ) : id( record ), tags( std::move( move ) ) {}

	Set( const Set& other ) = delete;
	Set& operator=( const Set& other ) = delete;

	Set( Set&& other ) = default;
	Set& operator=( Set&& other ) = default;
};

inline std::string tagToKey( const std::string_view namespace_i, const std::string_view subtag_i )
{
	if ( namespace_i == "" ) return std::string( subtag_i );
	return std::format( "{}:{}", namespace_i, subtag_i );
}

void HydrusImporter::processSets( const std::vector< Set >& sets, const TagDomainID domain_id )
{
	TransactionBase master_tr { master_db };

	std::vector< std::string > hashes {};

	std::vector< std::vector< std::pair< std::string, std::string > > > tag_sets {};

	// process hashes
	for ( const auto& set : sets )
	{
		bool invalid_hash { false };
		master_tr << "SELECT hex(hash) FROM hashes WHERE hash_id = $1" << set.id >>
			[ &hashes, &invalid_hash ]( const std::string_view hex_hash )
		{
			if ( hex_hash.size() != ( 256 / 8 * 2 ) )
			{
				invalid_hash = true;
				return;
			}

			hashes.emplace_back( hex_hash );
		};

		if ( invalid_hash ) continue;

		std::vector< std::pair< std::string, std::string > > tags {};
		tags.reserve( set.tags.size() );
		for ( const auto& tag_id : set.tags )
		{
			master_tr
					<< "SELECT namespace, subtag FROM tags NATURAL JOIN namespaces NATURAL JOIN subtags WHERE tag_id = $1"
					<< tag_id
				>> [ &tags ]( const std::string_view namespace_i, const std::string_view subtag_i )
			{ tags.emplace_back( namespace_i, subtag_i ); };
		}

		tag_sets.emplace_back( std::move( tags ) );
	}

	auto record_future { IDHANClient::instance().createRecords( hashes ) };

	record_future.waitForFinished();

	auto future { IDHANClient::instance().addTags( std::move( record_future.result() ), domain_id, std::move( tag_sets ) ) };

	future.waitForFinished();
}

void HydrusImporter::copyDomainMappings( const TagDomainID domain_id, const std::size_t hy_service_id )
{
	const auto table_name { std::format( "current_mappings_{}", hy_service_id ) };

	TransactionBase mappings_tr { mappings_db };

	logging::info( "Copying mappings from {}", table_name );

	std::vector< Set > sets {};
	std::vector< TagID > tags {};

	std::size_t current_id { 0 };

	std::size_t counter { 0 };
	std::size_t tag_counter { 0 };

	QThreadPool pool {};
	constexpr std::size_t thread_count { 2 };
	std::counting_semaphore< 64 > semaphore { thread_count };
	pool.setMaxThreadCount( thread_count );
	QFutureSynchronizer< void > sync {};

	constexpr std::size_t sets_per_request { 1024 * 8 };
	std::size_t to_process { 0 };

	mappings_tr << std::format( "SELECT COUNT(*) FROM {}", table_name ) >> to_process;

	logging::info( "Processing {} mappings for table {}", to_process, table_name );

	const auto finalizeSet = [ & ]()
	{
		semaphore.acquire();
		sync.addFuture(
			QtConcurrent::
				run( &pool,
		             [ sets = std::move( sets ), domain_id, &semaphore, this ]
		             {
						 if ( sets.empty() )
						 {
							 logging::critical( "wtf?" );
							 std::abort();
						 }
						 this->processSets( sets, domain_id );
						 semaphore.release();
					 } ) );
		sets.clear();
		sets.reserve( sets_per_request );
		counter = 0;
		tag_counter = 0;
	};

	const auto flushSet = [ & ]( const auto new_id )
	{
		if ( counter >= sets_per_request ) finalizeSet();

		tag_counter += tags.size();
		Set set { static_cast< RecordID >( current_id ), std::move( tags ) };
		tags.reserve( sets_per_request );
		tags.clear();

		sets.emplace_back( std::move( set ) );

		counter += 1;
		current_id = new_id;
	};

	mappings_tr << std::format( "SELECT hash_id, tag_id FROM {} ORDER BY hash_id", table_name ) >>
		[ & ]( const std::size_t hash_id, const std::size_t tag_id )
	{
		// set the inital id
		if ( current_id == 0 ) [[unlikely]]
			current_id = hash_id;

		if ( hash_id != current_id ) [[unlikely]]
			flushSet( hash_id );

		tags.emplace_back( tag_id );
	};

	if ( !sets.empty() )
	{
		flushSet( 0 ); // can use 0 here since it's unimportant
		finalizeSet();
	}

	sync.waitForFinished();

	pool.waitForDone();

	logging::info( "Finished processing {}", table_name );
}

} // namespace idhan::hydrus