#include "TagServiceWorker.hpp"

#include <moc_TagServiceWorker.cpp>

#include <QObject>
#include <QtConcurrent>

#include <fstream>

#include "sqlitehelper/Query.hpp"
#include "sqlitehelper/Transaction.hpp"
#include "sqlitehelper/TransactionBaseCoro.hpp"

TagServiceWorker::TagServiceWorker( QObject* parent, idhan::hydrus::HydrusImporter* importer ) :
  QObject( parent ),
  QRunnable(),
  m_service(),
  m_importer( importer )
{
	FGL_ASSERT( importer, "Importer was null!" );
	this->setAutoDelete( false );
	qRegisterMetaType< Phase >();
	qRegisterMetaType< std::chrono::steady_clock::time_point >();
}

void TagServiceWorker::setService( const idhan::hydrus::ServiceInfo& info )
{
	m_service = info;
	if ( m_service.name == "public tag repository" ) m_ptr = true;
}

void TagServiceWorker::reportProgress( const std::size_t count )
{
	if ( count == 0 ) return;

	emit phaseProgress( count, std::chrono::steady_clock::now() );
}

void TagServiceWorker::preprocess()
{
	const auto id { m_service.service_id };
	const auto current_mappings_name { std::format( "current_mappings_{}", id ) };

	idhan::hydrus::TransactionBaseCoro mappings_tr { m_importer->mappings_db };

	std::size_t mappings_counter { 0 };
	std::size_t parent_counter { 0 };
	std::size_t sibling_counter { 0 };

	// Batched COUNT(*) queries instead of full table scans
	emit statusMessage( "Counting tag mappings in Hydrus" );
	{
		idhan::hydrus::Query< std::size_t > count_query {
			mappings_tr, std::format( "SELECT COUNT(*) FROM {}", current_mappings_name )
		};

		for ( const auto& [ count ] : count_query )
		{
			mappings_counter = count;
		}
	}

	emit processedMaxMappings( mappings_counter );

	const auto current_parents_name { std::format( "current_tag_parents_{}", id ) };

	emit statusMessage( "Counting parent relationships in Hydrus" );
	{
		idhan::hydrus::TransactionBase client_tr { m_importer->client_db };
		client_tr << std::format( "SELECT COUNT(*) FROM {}", current_parents_name ) >> [ & ]( std::size_t count )
		{ parent_counter = count; };
	}
	emit processedMaxParents( parent_counter );

	const auto current_siblings_name { std::format( "current_tag_siblings_{}", id ) };
	emit statusMessage( "Counting aliases in Hydrus" );
	{
		idhan::hydrus::TransactionBase client_tr { m_importer->client_db };
		client_tr << std::format( "SELECT COUNT(*) FROM {}", current_siblings_name ) >> [ & ]( std::size_t count )
		{ sibling_counter = count; };
	}

	emit processedMaxAliases( sibling_counter );
}

void TagServiceWorker::processPairs( const std::vector< MappingPair >& pairs )
{
	if ( pairs.empty() ) return;

	FGL_ASSERT( m_importer, "Importer was null!" );
	emit statusMessage( QString( "Reading tag names for %L1 mappings from Hydrus" ).arg( pairs.size() ) );

	using HyHashID = int;
	using HyTagID = int;
	using TagPair = std::pair< std::string, std::string >;

	std::unordered_map< HyHashID, std::vector< HyTagID > > hy_hash_tag_map {};
	hy_hash_tag_map.reserve( pairs.size() );
	std::unordered_set< HyHashID > hy_hash_id_set {};
	hy_hash_id_set.reserve( pairs.size() );

	// First pass: collect unique hash and tag ids
	for ( const auto& [ hash_id, tag_id ] : pairs )
	{
		hy_hash_id_set.emplace( hash_id );
		hy_hash_tag_map[ hash_id ].emplace_back( tag_id );
	}

	// --- Batch query tag names ---
	std::unordered_map< HyTagID, TagPair > hy_tag_map {};
	{
		std::vector< HyTagID > all_tag_ids;
		all_tag_ids.reserve( pairs.size() );
		for ( const auto& [ hash_id, tag_id ] : pairs )
		{
			if ( !hy_tag_map.contains( tag_id ) )
			{
				all_tag_ids.emplace_back( tag_id );
				hy_tag_map[ tag_id ] = {}; // placeholder
			}
		}

		constexpr std::size_t CHUNK_SIZE = 500;
		for ( std::size_t offset = 0; offset < all_tag_ids.size(); offset += CHUNK_SIZE )
		{
			const auto chunk_end = std::min( offset + CHUNK_SIZE, all_tag_ids.size() );
			const auto chunk_size = chunk_end - offset;

			std::string sql =
				"SELECT tag_id, namespace, subtag FROM tags NATURAL JOIN namespaces NATURAL JOIN subtags WHERE tag_id IN (";
			for ( std::size_t i = 0; i < chunk_size; ++i )
			{
				if ( i > 0 ) sql += ", ";
				sql += "?";
			}
			sql += ")";

			idhan::hydrus::TransactionBase master_tr { m_importer->master_db };
			auto binder = master_tr << sql;
			for ( std::size_t i = 0; i < chunk_size; ++i )
			{
				binder << all_tag_ids[ offset + i ];
			}

			binder >> [ & ]( HyTagID tag_id, std::string_view ns, std::string_view sub )
			{ hy_tag_map[ tag_id ] = std::make_pair( std::string( ns ), std::string( sub ) ); };
		}
	}

	// --- Batch query hashes ---
	emit statusMessage( QString( "Reading %L1 file hashes from Hydrus" ).arg( hy_hash_id_set.size() ) );
	std::unordered_map< HyHashID, std::string > hash_map {};
	{
		std::vector< HyHashID > hash_ids_vec( hy_hash_id_set.begin(), hy_hash_id_set.end() );

		constexpr std::size_t CHUNK_SIZE = 500;
		for ( std::size_t offset = 0; offset < hash_ids_vec.size(); offset += CHUNK_SIZE )
		{
			const auto chunk_end = std::min( offset + CHUNK_SIZE, hash_ids_vec.size() );
			const auto chunk_size = chunk_end - offset;

			std::string sql = "SELECT hash_id, hex(hash) FROM hashes WHERE hash_id IN (";
			for ( std::size_t i = 0; i < chunk_size; ++i )
			{
				if ( i > 0 ) sql += ", ";
				sql += "?";
			}
			sql += ")";

			idhan::hydrus::TransactionBase master_tr { m_importer->master_db };
			auto binder = master_tr << sql;
			for ( std::size_t i = 0; i < chunk_size; ++i )
			{
				binder << hash_ids_vec[ offset + i ];
			}

			binder >> [ & ]( HyHashID hash_id, std::string_view hash_str )
			{
				if ( hash_str.size() == ( 256 / 8 * 2 ) )
				{
					hash_map.emplace( hash_id, std::string( hash_str ) );
				}
			};
		}
	}

	emit statusMessage( "Preparing tag mapping batch" );
	std::vector< TagPair > tags {};
	std::unordered_map< HyTagID, std::size_t > tag_indices {};
	tags.reserve( hy_tag_map.size() );

	std::vector< std::string > hashes {};
	hashes.reserve( hy_hash_id_set.size() );
	std::vector< std::vector< idhan::TagID > > tag_sets {};
	tag_sets.reserve( hy_hash_id_set.size() );

	for ( const auto& hash_id : hy_hash_id_set )
	{
		const auto hash_it = hash_map.find( hash_id );
		if ( hash_it == hash_map.end() ) continue;

		hashes.emplace_back( hash_it->second );
		for ( const auto tag_id : hy_hash_tag_map.at( hash_id ) )
		{
			const auto [ tag_it, inserted ] = tag_indices.try_emplace( tag_id, tags.size() );
			if ( inserted ) tags.emplace_back( hy_tag_map.at( tag_id ) );
		}
	}

	if ( hashes.empty() ) return;

	try
	{
		auto& client = idhan::IDHANClient::instance();
		emit statusMessage( QString( "Waiting for server to create or find %L1 records" ).arg( hashes.size() ) );
		auto record_future = client.createRecords( hashes );

		record_future.waitForFinished();
		auto records = record_future.result();
		FGL_ASSERT( records.size() == hashes.size(), "Records size was different from hashes size!" );
		emit statusMessage( QString( "Waiting for server to create or find %L1 tags" ).arg( tags.size() ) );
		auto tag_ids_future = client.createTags( tags );
		tag_ids_future.waitForFinished();
		const auto tag_ids = tag_ids_future.result();
		FGL_ASSERT( tag_ids.size() == tags.size(), "Tag IDs size was different from tag names size!" );

		emit statusMessage( "Preparing resolved tag mappings" );
		for ( const auto& hash_id : hy_hash_id_set )
		{
			if ( !hash_map.contains( hash_id ) ) continue;

			std::vector< idhan::TagID > record_tags {};
			record_tags.reserve( hy_hash_tag_map.at( hash_id ).size() );
			for ( const auto tag_id : hy_hash_tag_map.at( hash_id ) )
			{
				record_tags.emplace_back( tag_ids.at( tag_indices.at( tag_id ) ) );
			}
			tag_sets.emplace_back( std::move( record_tags ) );
		}

		FGL_ASSERT( records.size() == tag_sets.size(), "Records size was different from tag sets size!" );
		emit statusMessage( QString( "Waiting for server to add %L1 tag mappings to %L2 records" )
		                        .arg( pairs.size() )
		                        .arg( records.size() ) );
		auto tag_future = client.addTags( std::move( records ), tag_domain_id, std::move( tag_sets ) );

		tag_future.waitForFinished();
	}
	catch ( std::exception& e )
	{
		idhan::logging::error( "Got exception: {} when trying to create mappings", e.what() );
		throw;
	}
}

void TagServiceWorker::processParents( const std::vector< std::pair< idhan::TagID, idhan::TagID > >& pairs )
{
	if ( pairs.empty() ) return;

	emit statusMessage( QString( "Waiting for server to add %L1 parent relationships" ).arg( pairs.size() ) );
	auto& client = idhan::IDHANClient::instance();

	auto future = client.createParentRelationship( tag_domain_id, pairs );
	future.waitForFinished();
}

void TagServiceWorker::processSiblings( const std::vector< std::pair< idhan::TagID, idhan::TagID > >& pairs )
{
	if ( pairs.empty() ) return;

	emit statusMessage( QString( "Waiting for server to add %L1 aliases" ).arg( pairs.size() ) );
	auto& client = idhan::IDHANClient::instance();

	auto future = client.createAliasRelationship( tag_domain_id, pairs );
	future.waitForFinished();
}

void TagServiceWorker::processMappingsBatch(
	const idhan::hydrus::TransactionBaseCoro& mappings_tr,
	const std::string& current_mappings_name )
{
	std::vector< MappingPair > pairs {};
	constexpr std::size_t hash_limit { 1000 * 2 };

	pairs.reserve( hash_limit * 64 );

	emit statusMessage( "Reading and ordering tag mappings from Hydrus" );
	idhan::hydrus::Query< int, int > query {
		mappings_tr, std::format( "SELECT tag_id, hash_id FROM {} ORDER BY hash_id, tag_id", current_mappings_name )
	};

	std::size_t mappings_counter { 0 };

	std::unordered_set< int > hash_id_set {};

	auto last_id { 0 };

	auto dumpMappings = [ & ]()
	{
		const auto record_count { hash_id_set.size() };
		hash_id_set.clear();
		processPairs( pairs );
		pairs.clear();
		emit processedMappings( mappings_counter, record_count );
		reportProgress( mappings_counter );
		mappings_counter = 0;
		emit statusMessage( "Reading next tag mapping batch from Hydrus" );
	};

	for ( const auto& [ tag_id, hash_id ] : query )
	{
		// dump before processing the next item if we are over the limit
		const auto hash_changed { hash_id != last_id };
		if ( hash_id_set.size() >= hash_limit && hash_changed )
		{
			dumpMappings();
		}
		last_id = hash_id;

		mappings_counter += 1;
		pairs.emplace_back( hash_id, tag_id );
		hash_id_set.insert( hash_id );
	}

	dumpMappings();
}

void TagServiceWorker::importMappings()
{
	using namespace idhan::hydrus;
	const auto id { m_service.service_id };
	const auto current_mappings_name { std::format( "current_mappings_{}", id ) };

	TransactionBaseCoro mappings_tr { m_importer->mappings_db };

	auto& client = idhan::IDHANClient::instance();

	const std::string service_name { m_service.name.toStdString() };

	emit statusMessage( "Waiting for server to find the tag domain" );
	auto tag_domain_search_f { client.getTagDomain( service_name ) };
	tag_domain_search_f.waitForFinished();
	if ( auto result = tag_domain_search_f.result(); result.has_value() )
	{
		tag_domain_id = result.value();
	}
	else
	{
		emit statusMessage( "Waiting for server to create the tag domain" );
		auto tag_domain_f { client.createTagDomain( service_name ) };

		//TODO: Add handling for conflicting tag domains
		try
		{
			tag_domain_f.waitForFinished();
			tag_domain_id = tag_domain_f.result();
		}
		catch ( std::exception& e )
		{
			idhan::logging::error(
				"Got exception: {} when trying to create tag domain for {}", e.what(), service_name );
			throw;
		}
	}

	processMappingsBatch( mappings_tr, current_mappings_name );
	processRelationships();
}

void TagServiceWorker::processSiblings(
	const std::vector< std::pair< int, int > >& hy_siblings,
	const std::unordered_map< int, std::pair< std::string, std::string > >& tag_pairs,
	const std::unordered_map< int, idhan::TagID >& tag_translation_map,
	const std::size_t set_limit )
{
	std::vector< std::pair< idhan::TagID, idhan::TagID > > siblings {};

	// These tags cause issues atm so we will just blacklist them.
	// Opened lazily on the first failure so a clean import leaves no stray file.
	std::ofstream bad_ids {};

	for ( const auto& [ hy_bad_id, hy_good_id ] : hy_siblings )
	{
		if ( hy_bad_id == hy_good_id )
		{
			idhan::logging::warn(
				"Found alias that references itself ({}, {}), {}", hy_bad_id, hy_good_id, tag_pairs.at( hy_bad_id ) );
			emit warningOccurred( QString( "Skipped self-referencing alias %1" ).arg( hy_bad_id ) );
			reportProgress( 1 );
			continue;
		}

		try
		{
			const auto idhan_bad_id = tag_translation_map.at( hy_bad_id );
			const auto idhan_good_id = tag_translation_map.at( hy_good_id );

			if ( idhan_bad_id == idhan_good_id )
			{
				throw std::runtime_error(
					std::format( "Found alias that references itself {} == {}", idhan_bad_id, idhan_good_id ) );
			}

			siblings.emplace_back( idhan_bad_id, idhan_good_id );
			if ( siblings.size() >= set_limit )
			{
				processSiblings( siblings );
				emit processedAliases( siblings.size() );
				reportProgress( siblings.size() );
				siblings.clear();
			}
		}
		catch ( std::exception& e )
		{
			reportProgress( std::max( std::size_t( 1 ), siblings.size() ) );
			emit warningOccurred(
				QString( "Could not import alias %1 to %2: %3" ).arg( hy_bad_id ).arg( hy_good_id ).arg( e.what() ) );
			idhan::logging::error(
				"Hydrus set (bad_id, good_id) ({}, {}) caused an error while importing: {}",
				hy_bad_id,
				hy_good_id,
				e.what() );

			if ( !bad_ids.is_open() ) bad_ids.open( "bad_ids.txt", std::ios::app );
			if ( bad_ids )
			{
				const auto [ namespace_text, subtag_text ] = tag_pairs.at( hy_bad_id );
				const auto [ namespace_text_2, subtag_text_2 ] = tag_pairs.at( hy_good_id );
				bad_ids << "'";
				if ( namespace_text.empty() )
					bad_ids << subtag_text;
				else
					bad_ids << namespace_text << ":" << subtag_text;

				bad_ids << "','";

				if ( namespace_text_2.empty() )
					bad_ids << subtag_text_2;
				else
					bad_ids << namespace_text_2 << ":" << subtag_text_2;

				bad_ids << "'";
				bad_ids << '\n';
			}

			// Do nothing
			siblings.clear();
		}
	}
	processSiblings( siblings );
	emit processedAliases( siblings.size() );
	reportProgress( siblings.size() );
}

void TagServiceWorker::processParents(
	const std::vector< std::pair< int, int > >& hy_parents,
	const std::unordered_map< int, idhan::TagID >& tag_translation_map,
	const std::size_t set_limit )
{
	std::vector< std::pair< idhan::TagID, idhan::TagID > > parents {};

	for ( const auto& [ hy_child_id, hy_parent_id ] : hy_parents )
	{
		try
		{
			const auto idhan_child_id = tag_translation_map.at( hy_child_id );
			const auto idhan_parent_id = tag_translation_map.at( hy_parent_id );

			// parents are expected to be in (parent, child) format
			parents.emplace_back( idhan_parent_id, idhan_child_id );

			if ( parents.size() >= set_limit )
			{
				processParents( parents );
				emit processedParents( parents.size() );
				reportProgress( parents.size() );
				parents.clear();
			}
		}
		catch ( std::exception& e )
		{
			reportProgress( std::max( std::size_t( 1 ), parents.size() ) );
			emit warningOccurred( QString( "Could not import parent %1 of %2: %3" )
			                          .arg( hy_parent_id )
			                          .arg( hy_child_id )
			                          .arg( e.what() ) );
			idhan::logging::error(
				"Hydrus parent set (child_id, parent_id) ({}, {}) caused an error while importing: {}",
				hy_child_id,
				hy_parent_id,
				e.what() );

			// Drop the partially-built batch so one bad relationship can't corrupt the set
			parents.clear();
		}
	}

	processParents( parents );
	emit processedParents( parents.size() );
	reportProgress( parents.size() );
}

void TagServiceWorker::processRelationships()
{
	emit phaseStarted( Phase::PreparingRelationships, std::chrono::steady_clock::now() );
	using namespace idhan::hydrus;

	const auto id { m_service.service_id };
	const auto current_parents_name { std::format( "current_tag_parents_{}", id ) };
	const auto current_siblings_name { std::format( "current_tag_siblings_{}", id ) };

	TransactionBaseCoro client_tr { m_importer->client_db };
	TransactionBaseCoro master_tr { m_importer->master_db };

	using HyTagID = int;
	using ParentID = HyTagID;
	using ChildID = HyTagID;
	using BadTagID = HyTagID;
	using GoodTagID = HyTagID;

	std::set< HyTagID > tag_set {};

	std::vector< std::pair< ChildID, ParentID > > hy_parents {};
	emit statusMessage( "Reading parent relationships from Hydrus" );
	// Get all parent mappings, insert into the tag_set set to get a list of unique ids
	{
		Query< ParentID, ChildID > query {
			client_tr, std::format( "SELECT child_tag_id, parent_tag_id FROM {}", current_parents_name )
		};

		for ( const auto& [ child_id, parent_id ] : query )
		{
			hy_parents.emplace_back( child_id, parent_id );
			tag_set.emplace( child_id );
			tag_set.emplace( parent_id );
		}
	}

	std::vector< std::pair< BadTagID, GoodTagID > > hy_siblings {};
	emit statusMessage( "Reading aliases from Hydrus" );
	// Get all the sibling, insert into the tag_set set to get a list of unique ids
	{
		Query< int, int > query {
			client_tr, std::format( "SELECT bad_tag_id, good_tag_id FROM {}", current_siblings_name )
		};

		for ( const auto& [ bad_id, good_id ] : query )
		{
			hy_siblings.emplace_back( bad_id, good_id );
			tag_set.emplace( bad_id );
			tag_set.emplace( good_id );
		}
	}

	std::unordered_map< int, std::pair< std::string, std::string > > tag_pairs {};
	emit statusMessage( QString( "Reading %L1 relationship tag names from Hydrus" ).arg( tag_set.size() ) );
	// Batch query all unique tag names in one go instead of N+1 individual queries
	{
		std::vector< HyTagID > tag_ids_vec( tag_set.begin(), tag_set.end() );

		constexpr std::size_t CHUNK_SIZE = 500;
		for ( std::size_t offset = 0; offset < tag_ids_vec.size(); offset += CHUNK_SIZE )
		{
			const auto chunk_end = std::min( offset + CHUNK_SIZE, tag_ids_vec.size() );
			const auto chunk_size = chunk_end - offset;

			std::string sql =
				"SELECT tag_id, namespace, subtag FROM tags NATURAL JOIN namespaces NATURAL JOIN subtags WHERE tag_id IN (";
			for ( std::size_t i = 0; i < chunk_size; ++i )
			{
				if ( i > 0 ) sql += ", ";
				sql += "?";
			}
			sql += ")";

			TransactionBase tag_tr { m_importer->master_db };
			auto binder = tag_tr << sql;
			for ( std::size_t i = 0; i < chunk_size; ++i )
			{
				binder << tag_ids_vec[ offset + i ];
			}

			binder >> [ & ]( HyTagID tag_id, std::string_view ns, std::string_view sub )
			{ tag_pairs.emplace( tag_id, std::make_pair( std::string( ns ), std::string( sub ) ) ); };
		}
	}

	std::vector< std::pair< std::string, std::string > > tags {};
	std::vector< HyTagID > tag_order {};
	std::unordered_map< HyTagID, idhan::TagID > tag_translation_map {};
	auto& client { idhan::IDHANClient::instance() };
	tag_translation_map.reserve( tag_pairs.size() );

	auto flushTags = [ & ]()
	{
		emit statusMessage(
			QString( "Waiting for server to create or find %L1 relationship tags" ).arg( tags.size() ) );
		auto tag_f { client.createTags( tags ) };
		tag_f.waitForFinished();
		const auto result { tag_f.result() };

		FGL_ASSERT( tag_order.size() == result.size(), "Tag set was not the same size as result!" );

		auto ret_itter = result.begin();
		auto itter = tag_order.begin();
		for ( ; itter != tag_order.end(); ++itter, ++ret_itter ) tag_translation_map.emplace( *itter, *ret_itter );

		tags.clear();
		tag_order.clear();
	};

	for ( const auto& [ tag_id, tag_text ] : tag_pairs )
	{
		tags.emplace_back( tag_text );
		tag_order.emplace_back( tag_id );

		if ( tags.size() >= 1024 * 4 )
		{
			flushTags();
		}
	}

	if ( !tags.empty() )
	{
		flushTags();
	}

	idhan::logging::debug( "Created {} tags", tag_translation_map.size() );

	// For now we are limiting to 1 set until we can make a better rollback system
	constexpr std::size_t set_limit { 1 };

	emit phaseStarted( Phase::Aliases, std::chrono::steady_clock::now() );
	processSiblings( hy_siblings, tag_pairs, tag_translation_map, set_limit );

	// Process parents
	emit phaseStarted( Phase::Parents, std::chrono::steady_clock::now() );
	processParents( hy_parents, tag_translation_map, set_limit );
}

void TagServiceWorker::run()
{
	try
	{
		if ( !m_preprocessed )
		{
			preprocess();
			m_preprocessed = true;
			emit preprocessingFinished( true );
		}
		else
		{
			emit phaseStarted( Phase::Mappings, std::chrono::steady_clock::now() );
			importMappings();
			emit finished();
		}
	}
	catch ( std::exception& e )
	{
		idhan::logging::error( e.what() );
		emit errorOccurred( QString::fromStdString( e.what() ) );
		if ( !m_preprocessed ) emit preprocessingFinished( false );
	}
	catch ( ... )
	{
		idhan::logging::error( "Unknown exception" );
		emit errorOccurred( "Unknown exception during import" );
		if ( !m_preprocessed ) emit preprocessingFinished( false );
	}

	return;
}
