//
// Created by kj16609 on 11/5/25.
//
#include "FileRelationshipsWorker.hpp"

#include <moc_FileRelationshipsWorker.cpp>

#include "sqlitehelper/Query.hpp"
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
	idhan::hydrus::TransactionBaseCoro client_tr { m_importer->client_db };

	idhan::hydrus::Query< int, int > duplicate_file_members {
		client_tr,
		"SELECT hash_id, king_hash_id FROM duplicate_file_members dfm JOIN duplicate_files df ON dfm.media_id = df.media_id WHERE hash_id != king_hash_id"
	};

	std::size_t duplicate_file_members_counter { 0 };

	for ( [[maybe_unused]] const auto& [ media_id, hash_id ] : duplicate_file_members )
	{
		duplicate_file_members_counter++;

		if ( duplicate_file_members_counter % 1000 == 0 ) emit processedMaxDuplicates( duplicate_file_members_counter );
	}

	emit processedMaxDuplicates( duplicate_file_members_counter );

	idhan::hydrus::Query< int, int > alternative_file_members {
		client_tr, "SELECT * FROM alternate_file_group_members JOIN duplicate_file_members USING (media_id)"
	};
	std::size_t alternative_file_members_counter { 0 };

	for ( [[maybe_unused]] const auto& [ media_id, hash_id ] : alternative_file_members )
	{
		alternative_file_members_counter++;

		if ( alternative_file_members_counter % 1000 == 0 )
			emit processedMaxAlternatives( alternative_file_members_counter );
	}

	emit processedMaxAlternatives( alternative_file_members_counter );
}

void FileRelationshipsWorker::process()
{
	idhan::hydrus::TransactionBaseCoro client_tr { m_importer->client_db };

	using MediaID = std::uint32_t;
	using HashID = std::uint32_t;
	using KingID = std::uint32_t;

	std::vector< HashID > hash_ids {};

	std::unordered_map< HashID, idhan::RecordID > record_map {};

	emit statusMessage( "Started" );

	auto flushHashIDs = [ &, this ]()
	{
		emit statusMessage( "Mapping Hydrus IDs to IDHAN IDs" );
		std::ranges::sort( hash_ids );
		std::ranges::unique( hash_ids );

		std::ranges::remove_if(
			hash_ids, [ &record_map ]( const HashID hash_id ) -> bool { return record_map.contains( hash_id ); } );

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

		emit statusMessage( "Setting duplicates for batch" );

		std::vector< std::pair< idhan::RecordID, idhan::RecordID > > idhan_pairs {};

		for ( const auto& [ hy_hash_id, hy_king_id ] : pairs )
		{
			const auto idhan_hash_id { record_map.at( hy_hash_id ) };
			const auto idhan_king_id { record_map.at( hy_king_id ) };

			idhan_pairs.emplace_back( idhan_hash_id, idhan_king_id );

			// auto future = client.setDuplicates( pairs );
			// future.waitForFinished();
		}

		auto future { client.setDuplicates( idhan_pairs ) };
		future.waitForFinished();

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
			emit statusMessage( "Getting additional rows to process" );
		}
	}

	flushPairs();

	idhan::hydrus::Query< HashID, MediaID > alternative_files {
		client_tr,
		"SELECT hash_id, alternates_group_id FROM alternate_file_group_members JOIN duplicate_file_members USING (media_id);"
	};

	using GroupID = std::uint32_t;

	std::unordered_map< GroupID, std::vector< idhan::hydrus::HashID > > alternative_map {};

	emit statusMessage( "Mapping alternative hashes to IDHAN" );

	for ( const auto& [ hash_id, alternative_group_id ] : alternative_files )
	{
		hash_ids.push_back( hash_id );

		if ( hash_ids.size() >= 64 ) flushHashIDs();

		if ( auto itter = alternative_map.find( alternative_group_id ); itter != alternative_map.end() )
			itter->second.push_back( hash_id );
		else
			alternative_map.emplace( alternative_group_id, std::vector< idhan::hydrus::HashID > { hash_id } );
	}

	flushHashIDs();

	emit statusMessage( "Setting alternatives for groups" );

	std::size_t alternative_count { 0 };

	for ( const auto hy_hashes : alternative_map | std::views::values )
	{
		std::vector< idhan::RecordID > record_ids {};
		for ( const auto& hy_hash : hy_hashes ) record_ids.emplace_back( record_map.at( hy_hash ) );

		alternative_count += record_ids.size();

		auto future = client.setAlternativeGroups( record_ids );
		future.waitForFinished();
		emit processedAlternatives( alternative_count );
	}

	emit statusMessage( "Finished" );
}

void FileRelationshipsWorker::run()
{
	if ( !m_preprocessed )
	{
		m_preprocessed = true;
		preprocess();
		return;
	}

	process();
}
