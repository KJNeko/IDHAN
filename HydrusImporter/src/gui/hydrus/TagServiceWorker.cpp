//
// Created by kj16609 on 6/29/25.
//

#include "TagServiceWorker.hpp"

#include <QObject>
#include <QtConcurrent>

#include <fstream>

#include "moc_TagServiceWorker.cpp"
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
}

void TagServiceWorker::setService( const idhan::hydrus::ServiceInfo& info )
{
	m_service = info;
	if ( m_service.name == "public tag repository" ) m_ptr = true;
}

void TagServiceWorker::preprocess()
{
	const auto id { m_service.service_id };
	const auto current_mappings_name { std::format( "current_mappings_{}", id ) };

	idhan::hydrus::TransactionBaseCoro mappings_tr { m_importer->mappings_db };

	std::size_t mappings_counter { 0 };
	std::size_t parent_counter { 0 };
	std::size_t sibling_counter { 0 };

	idhan::hydrus::Query< int, int > query { mappings_tr, std::format( "SELECT * FROM {}", current_mappings_name ) };

	for ( [[maybe_unused]] const auto& [ tag_id, hash_id ] : query )
	{
		mappings_counter += 1;
		if ( mappings_counter % 500'000 == 0 ) emit processedMaxMappings( mappings_counter );
	};

	emit processedMaxMappings( mappings_counter );

	const auto current_parents_name { std::format( "current_tag_parents_{}", id ) };

	idhan::hydrus::TransactionBase client_tr { m_importer->client_db };

	client_tr << std::format( "SELECT * FROM {}", current_parents_name ) >>
		[ & ]( [[maybe_unused]] const int child_id, [[maybe_unused]] const int parent_id )
	{
		parent_counter += 1;
		if ( parent_counter % 64'000 == 0 ) emit processedMaxParents( parent_counter );
	};
	emit processedMaxParents( parent_counter );

	const auto current_siblings_name { std::format( "current_tag_siblings_{}", id ) };
	client_tr << std::format( "SELECT * FROM {}", current_siblings_name ) >>
		[ & ]( [[maybe_unused]] const int bad_id, [[maybe_unused]] const int good_id )
	{
		sibling_counter += 1;
		if ( sibling_counter % 64'000 ) emit processedMaxAliases( sibling_counter );
	};

	emit processedMaxAliases( sibling_counter );

	m_preprocessed = true;

	emit finished();
}

void TagServiceWorker::processPairs( const std::vector< MappingPair >& pairs ) const
{
	FGL_ASSERT( m_importer, "Importer was null!" );
	idhan::hydrus::TransactionBase master_tr { m_importer->master_db };

	using HyHashID = int;
	using HyTagID = int;
	using TagPair = std::pair< std::string, std::string >;

	std::unordered_map< HyHashID, std::vector< HyTagID > > hy_hash_tag_map {};
	hy_hash_tag_map.reserve( pairs.size() );
	std::unordered_set< HyHashID > hy_hash_id_set {};
	hy_hash_id_set.reserve( pairs.size() );
	std::unordered_map< HyTagID, TagPair > hy_tag_map {};
	hy_tag_map.reserve( pairs.size() );

	// process tag ids
	for ( const auto& [ hash_id, tag_id ] : pairs )
	{
		hy_hash_id_set.emplace( hash_id );

		if ( !hy_hash_tag_map.contains( hash_id ) )
		{
			hy_hash_tag_map[ hash_id ] = {};
		}

		hy_hash_tag_map[ hash_id ].emplace_back( tag_id );

		if ( !hy_tag_map.contains( tag_id ) )
		{
			TagPair tag_pair {};

			idhan::hydrus::TransactionBaseCoro master_tr_coro { m_importer->master_db };
			idhan::hydrus::Query< std::string_view, std::string_view > query {
				master_tr_coro,
				"SELECT namespace, subtag FROM tags NATURAL JOIN namespaces NATURAL JOIN subtags WHERE tag_id = $1",
				tag_id
			};

			const auto [ namespace_id, subtag_i ] = *query;
			tag_pair = std::make_pair( namespace_id, subtag_i );
			hy_tag_map[ tag_id ] = tag_pair;
		}
	}

	std::vector< std::string > hashes {};
	hashes.reserve( hy_hash_id_set.size() );
	std::vector< std::vector< TagPair > > tag_sets {};
	tag_sets.reserve( hy_hash_id_set.size() );

	for ( const auto& hash_id : hy_hash_id_set )
	{
		std::string hash {};
		hash.reserve( 256 / 8 );
		bool invalid_hash { false };
		master_tr << "SELECT hex(hash) FROM hashes WHERE hash_id = $1" << hash_id >>
			[ & ]( const std::string_view hash_i )
		{
			if ( hash_i.size() != ( 256 / 8 * 2 ) ) invalid_hash = true;
			hash = hash_i;
		};

		if ( invalid_hash ) continue;

		hashes.emplace_back( hash );

		std::vector< TagPair > tag_pairs {};
		tag_pairs.reserve( hy_hash_tag_map[ hash_id ].size() );
		for ( const auto& tag_id : hy_hash_tag_map[ hash_id ] )
		{
			tag_pairs.emplace_back( hy_tag_map[ tag_id ] );
		}

		tag_sets.emplace_back( std::move( tag_pairs ) );
	}

	// Get unique tag ids

	using namespace idhan;

	mappings_semaphore.acquire();
	while ( !m_futures.empty() && m_futures.front().isFinished() )
	{
		m_futures.pop();
	}

	m_futures.push(
		QtConcurrent::run(
			[ this, hashes = std::move( hashes ), tag_sets_i = std::move( tag_sets ) ]() mutable
			{
				try
				{
					auto& client = idhan::IDHANClient::instance();
					auto record_future = client.createRecords( hashes );
					auto tag_sets_c { tag_sets_i };

					record_future.waitForFinished();
					auto records = record_future.result();
					FGL_ASSERT( records.size() == hashes.size(), "Records size was different from hashes size!" );
					FGL_ASSERT( records.size() == tag_sets_c.size(), "Records size was different from tag sets size!" );
					auto tag_future = client.addTags( std::move( records ), tag_domain_id, std::move( tag_sets_c ) );

					tag_future.waitForFinished();
					mappings_semaphore.release();
				}
				catch ( std::exception& e )
				{
					idhan::logging::error( "Got exception: {} when trying to create mappings", e.what() );
					std::abort();
				}
			} ) );
}

void TagServiceWorker::processParents( const std::vector< std::pair< idhan::TagID, idhan::TagID > >& pairs ) const
{
	auto& client = idhan::IDHANClient::instance();

	auto future = client.createParentRelationship( tag_domain_id, pairs );
	future.waitForFinished();
}

void TagServiceWorker::processSiblings( const std::vector< std::pair< idhan::TagID, idhan::TagID > >& pairs ) const
{
	auto& client = idhan::IDHANClient::instance();

	auto future = client.createAliasRelationship( tag_domain_id, pairs );
	future.waitForFinished();
}

void TagServiceWorker::processMappingsBatch(
	const idhan::hydrus::TransactionBaseCoro& mappings_tr,
	const std::string& current_mappings_name )
{
	std::vector< MappingPair > pairs {};
	constexpr std::size_t hash_limit { 100 };
	constexpr std::size_t average_tags_per_hash { 64 };
	constexpr std::size_t pair_limit { average_tags_per_hash * hash_limit };

	pairs.reserve( pair_limit );

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
		mappings_counter = 0;
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

	auto tag_domain_f { client.createTagDomain( service_name ) };

	//TODO: Add handling for conflicting tag domains
	try
	{
		tag_domain_f.waitForFinished();
	}
	catch ( std::exception& e )
	{
		idhan::logging::info( "Got exception: {} when trying to create tag domain for {}", e.what(), service_name );
		tag_domain_f = client.getTagDomain( service_name );
		tag_domain_f.waitForFinished();
	}

	tag_domain_id = tag_domain_f.result();

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

	// These tags cause issues atm so we will just blacklist them

	for ( const auto& [ hy_bad_id, hy_good_id ] : hy_siblings )
	{
		if ( hy_bad_id == hy_good_id )
		{
			idhan::logging::warn(
				"Found alias that references itself ({}, {}), {}", hy_bad_id, hy_good_id, tag_pairs.at( hy_bad_id ) );
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
				siblings.clear();
			}
		}
		catch ( std::exception& e )
		{
			idhan::logging::error(
				"Hydrus set (bad_id, good_id) ({}, {}) caused an error while importing: {}",
				hy_bad_id,
				hy_good_id,
				e.what() );

			if ( std::ofstream ofs( "bad_ids.txt", std::ios::app ); ofs )
			{
				const auto [ namespace_text, subtag_text ] = tag_pairs.at( hy_bad_id );
				const auto [ namespace_text_2, subtag_text_2 ] = tag_pairs.at( hy_good_id );
				ofs << "'";
				if ( namespace_text.empty() )
					ofs << subtag_text;
				else
					ofs << namespace_text << ":" << subtag_text;

				ofs << "','";

				if ( namespace_text_2.empty() )
					ofs << subtag_text_2;
				else
					ofs << namespace_text_2 << ":" << subtag_text_2;

				ofs << "'";
				ofs << std::endl;
			}

			// Do nothing
			siblings.clear();
		}
	}
	processSiblings( siblings );
	emit processedAliases( siblings.size() );
}

void TagServiceWorker::processParents(
	const std::vector< std::pair< int, int > >& hy_parents,
	const std::unordered_map< int, idhan::TagID >& tag_translation_map,
	const std::size_t set_limit )
{
	std::vector< std::pair< idhan::TagID, idhan::TagID > > parents {};

	for ( const auto& [ hy_child_id, hy_parent_id ] : hy_parents )
	{
		const auto idhan_child_id = tag_translation_map.at( hy_child_id );
		const auto idhan_parent_id = tag_translation_map.at( hy_parent_id );

		// parents are expected to be in (parent, child) format
		parents.emplace_back( idhan_parent_id, idhan_child_id );

		if ( parents.size() >= set_limit )
		{
			processParents( parents );
			emit processedParents( parents.size() );
			parents.clear();
		}
	}

	processParents( parents );
	emit processedParents( parents.size() );
}

void TagServiceWorker::processRelationships()
{
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
	// get all the unique tags component ids and their associated strings
	for ( const auto& tag_id : tag_set )
	{
		Query< int, int > tag_query { master_tr, "SELECT namespace_id, subtag_id FROM tags WHERE tag_id = $1", tag_id };

		const auto& [ namespace_id, subtag_id ] = *tag_query;

		Query< std::string_view > namespace_query {
			master_tr, "SELECT namespace FROM namespaces WHERE namespace_id = $1", namespace_id
		};

		const auto& [ namespace_text_v ] = *namespace_query;
		std::string namespace_text { namespace_text_v };

		Query< std::string_view > subtag_query {
			master_tr, "SELECT subtag FROM subtags WHERE subtag_id = $1", subtag_id
		};

		const auto& [ subtag_text_v ] = *subtag_query;
		std::string subtag_text { subtag_text_v };

		tag_pairs.emplace( tag_id, std::make_pair( namespace_text, subtag_text ) );
	}

	std::vector< std::pair< std::string, std::string > > tags {};
	std::vector< HyTagID > tag_order {};
	std::unordered_map< HyTagID, idhan::TagID > tag_translation_map {};
	auto& client { idhan::IDHANClient::instance() };
	tag_translation_map.reserve( tag_pairs.size() );

	auto flushTags = [ & ]()
	{
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
		// const auto& [ namespace_str, subtag_str ] = tag_text;
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

	// idhan::logging::debug( "Finished processing tags" );

	// For now we are limiting to 1 set until we can make a better rollback system
	constexpr std::size_t set_limit { 1 };

	processSiblings( hy_siblings, tag_pairs, tag_translation_map, set_limit );

	// Process parents
	processParents( hy_parents, tag_translation_map, set_limit );
}

void TagServiceWorker::run()
{
	try
	{
		if ( !m_preprocessed )
		{
			m_preprocessed = true;
			preprocess();
		}
		else
		{
			m_processing = true;
			importMappings();
			emit finished();
		}
	}
	catch ( std::exception& e )
	{
		idhan::logging::error( e.what() );
		std::terminate();
	}
	catch ( ... )
	{
		idhan::logging::error( "Unknown exception" );
		std::terminate();
	}
}
