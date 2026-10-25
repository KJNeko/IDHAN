#include "FileRelationshipsWorker.hpp"

#include <moc_FileRelationshipsWorker.cpp>

#include "sqlitehelper/Query.hpp"
#include "sqlitehelper/Transaction.hpp"
#include "sqlitehelper/TransactionBaseCoro.hpp"

FileRelationshipsWorker::FileRelationshipsWorker( QObject* parent, idhan::hydrus::HydrusImporter* importer ) :
  QObject( parent ),
  QRunnable(),
  m_importer( importer )
{
	this->setAutoDelete( false );
}

void FileRelationshipsWorker::preprocess()
{
	emit statusMessage( "Counting duplicate relationships in Hydrus" );
	{
		idhan::hydrus::TransactionBase client_tr { m_importer->client_db };

		client_tr
		        << "SELECT COUNT(*) FROM duplicate_file_members dfm JOIN duplicate_files df ON dfm.media_id = df.media_id WHERE hash_id != king_hash_id"
		    >> [ & ]( std::size_t count ) { emit processedMaxDuplicates( count ); };
	}

	emit statusMessage( "Counting alternative file relationships in Hydrus" );
	{
		idhan::hydrus::TransactionBase client_tr { m_importer->client_db };

		client_tr << "SELECT COALESCE( SUM( member_count ), 0 ) FROM "
					 "( SELECT COUNT(*) AS member_count FROM alternate_file_group_members "
					 "JOIN duplicate_files USING (media_id) GROUP BY alternates_group_id ) grouped "
					 "WHERE member_count > 1"
		    >> [ & ]( std::size_t count ) { emit processedMaxAlternatives( count ); };
	}
}

void FileRelationshipsWorker::process()
{
	idhan::hydrus::TransactionBaseCoro client_tr { m_importer->client_db };

	using HashID = std::uint32_t;
	using KingID = std::uint32_t;

	std::vector< HashID > hash_ids {};

	std::unordered_map< HashID, idhan::RecordID > record_map {};

	emit statusMessage( "Reading duplicate relationships from Hydrus" );

	auto flushHashIDs = [ &, this ]()
	{
		emit statusMessage( "Resolving file hashes and creating records on server" );
		std::ranges::sort( hash_ids );
		const auto duplicates { std::ranges::unique( hash_ids ) };
		hash_ids.erase( duplicates.begin(), duplicates.end() );

		const auto already_mapped { std::ranges::remove_if(
			hash_ids, [ &record_map ]( const HashID hash_id ) -> bool { return record_map.contains( hash_id ); } ) };
		hash_ids.erase( already_mapped.begin(), already_mapped.end() );

		if ( hash_ids.empty() ) return;

		const auto batch_map { m_importer->mapHydrusRecords( hash_ids ) };
		// merge the new map into the existing one

		for ( const auto& [ hy_hash_id, idhan_hash_id ] : batch_map )
			record_map.insert_or_assign( hy_hash_id, idhan_hash_id );

		hash_ids.clear();
	};

	idhan::hydrus::Query< HashID, KingID > duplicate_files {
		client_tr,
		"SELECT hash_id, king_hash_id FROM duplicate_file_members dfm JOIN duplicate_files df ON dfm.media_id = df.media_id WHERE hash_id != king_hash_id"
	};

	std::vector< std::pair< HashID, KingID > > pairs {};

	auto& client { idhan::IDHANClient::instance() };

	std::size_t processed_count { 0 };

	auto flushPairs = [ &, this ]()
	{
		flushHashIDs();

		emit statusMessage( "Preparing duplicate relationship batch" );

		std::vector< std::pair< idhan::RecordID, idhan::RecordID > > idhan_pairs {};
		idhan_pairs.reserve( pairs.size() );

		for ( const auto& [ hy_hash_id, hy_king_id ] : pairs )
		{
			const auto hash_it { record_map.find( hy_hash_id ) };
			const auto king_it { record_map.find( hy_king_id ) };

			if ( hash_it == record_map.end() || king_it == record_map.end() ) continue;

			idhan_pairs.emplace_back( hash_it->second, king_it->second );
		}

		try
		{
			emit statusMessage(
				QString( "Waiting for server to set %L1 duplicate relationships" ).arg( idhan_pairs.size() ) );
			auto future { client.setDuplicates( idhan_pairs ) };
			future.waitForFinished();
		}
		catch ( const std::exception& e )
		{
			emit warningOccurred( QString( "Duplicate batch failed: %1" ).arg( e.what() ) );
			idhan::logging::error( "Failed to set duplicate batch of {} pairs: {}", idhan_pairs.size(), e.what() );
		}

		processed_count += pairs.size();
		emit processedDuplicates( processed_count );

		pairs.clear();
	};

	for ( const auto& [ hash_id, king_id ] : duplicate_files )
	{
		if ( hash_id == king_id ) continue;

		if ( !record_map.contains( hash_id ) ) hash_ids.push_back( hash_id );
		if ( !record_map.contains( king_id ) ) hash_ids.push_back( king_id );

		pairs.emplace_back( std::make_pair( hash_id, king_id ) );

		if ( pairs.size() >= 100 )
		{
			flushPairs();
			emit statusMessage( "Reading next duplicate relationship batch from Hydrus" );
		}
	}

	flushPairs();

	using GroupID = std::uint32_t;

	emit statusMessage( "Reading alternative file groups from Hydrus" );
	idhan::hydrus::Query< HashID, GroupID > alternative_files {
		client_tr,
		"SELECT king_hash_id, alternates_group_id FROM alternate_file_group_members JOIN duplicate_files USING (media_id)"
	};

	std::unordered_map< GroupID, std::vector< idhan::hydrus::HashID > > alternative_map {};

	emit statusMessage( "Preparing alternative file groups" );

	for ( const auto& [ king_hash_id, alternative_group_id ] : alternative_files )
	{
		hash_ids.push_back( king_hash_id );

		if ( hash_ids.size() >= 64 ) flushHashIDs();

		if ( auto itter = alternative_map.find( alternative_group_id ); itter != alternative_map.end() )
			itter->second.push_back( king_hash_id );
		else
			alternative_map.emplace( alternative_group_id, std::vector< idhan::hydrus::HashID > { king_hash_id } );
	}

	flushHashIDs();

	emit statusMessage( "Preparing alternative relationships" );

	std::size_t alternative_count { 0 };

	for ( const auto& hy_hashes : alternative_map | std::views::values )
	{
		std::vector< idhan::RecordID > record_ids {};
		record_ids.reserve( hy_hashes.size() );
		for ( const auto& hy_hash : hy_hashes )
		{
			const auto itter { record_map.find( hy_hash ) };
			if ( itter != record_map.end() ) record_ids.emplace_back( itter->second );
		}

		// A record may be shared between kings in the group; de-duplicate before pairing them up.
		std::ranges::sort( record_ids );
		const auto dupes { std::ranges::unique( record_ids ) };
		record_ids.erase( dupes.begin(), dupes.end() );

		if ( record_ids.size() < 2 ) continue;

		try
		{
			emit statusMessage(
				QString( "Waiting for server to set alternatives for %L1 records" ).arg( record_ids.size() ) );
			auto future = client.setAlternatives( record_ids );
			future.waitForFinished();

			alternative_count += record_ids.size();
			emit processedAlternatives( alternative_count );
		}
		catch ( const std::exception& e )
		{
			emit warningOccurred( QString( "Alternative group failed: %1" ).arg( e.what() ) );
			idhan::logging::error( "Failed to set alternatives of {} records: {}", record_ids.size(), e.what() );
		}
	}
}

void FileRelationshipsWorker::run()
{
	try
	{
		if ( !m_preprocessed )
		{
			preprocess();
			m_preprocessed = true;
			emit preprocessingFinished( true );
			return;
		}

		process();
		emit finished();
	}
	catch ( std::exception& e )
	{
		idhan::logging::error( e.what() );
		emit errorOccurred( QString::fromStdString( e.what() ) );
		if ( !m_preprocessed ) emit preprocessingFinished( false );
	}
	catch ( ... )
	{
		idhan::logging::error( "Unknown exception in FileRelationshipsWorker" );
		emit errorOccurred( "Unknown exception during file relationship import" );
		if ( !m_preprocessed ) emit preprocessingFinished( false );
	}
}
