//
// Created by kj16609 on 6/29/25.
//

#include "TagServiceWorker.hpp"

#include <QObject>

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
}

void TagServiceWorker::preprocess()
{
	const auto id { m_service.service_id };
	const auto current_mappings_name { std::format( "current_mappings_{}", id ) };

	idhan::hydrus::TransactionBaseCoro mappings_tr { m_importer->mappings_db };

	std::size_t mappings_counter { 0 };
	std::size_t parent_counter { 0 };
	std::size_t sibling_counter { 0 };

	if ( m_ptr && false ) // Disabled until I can figure why this locks up the GUI when it runs
	{
		if ( QThread::isMainThread() ) throw std::runtime_error( "Why the fuck is this on the main thread?" );

		idhan::hydrus::Query< std::size_t > query { mappings_tr,
			                                        std::format( "SELECT count(*) FROM {}", current_mappings_name ) };

		for ( const auto& [ mapping_count ] : query )
		{
			mappings_counter = mapping_count;
		}

		// mappings_tr << std::format( "SELECT count(*) FROM {}", current_mappings_name ) >> mappings_counter;
	}
	else
	{
		idhan::hydrus::Query< int, int > query { mappings_tr,
			                                     std::format( "SELECT * FROM {}", current_mappings_name ) };

		for ( const auto& [ tag_id, hash_id ] : query )
		{
			mappings_counter += 1;
			if ( mappings_counter % 500'000 == 0 ) emit processedMaxMappings( mappings_counter );
		};
	}

	emit processedMaxMappings( mappings_counter );

	const auto current_parents_name { std::format( "current_tag_parents_{}", id ) };

	idhan::hydrus::TransactionBase client_tr { m_importer->client_db };

	client_tr << std::format( "SELECT * FROM {}", current_parents_name ) >>
		[ & ]( const int child_id, const int parent_id )
	{
		parent_counter += 1;
		if ( parent_counter % 64'000 == 0 ) emit processedMaxParents( parent_counter );
	};
	emit processedMaxParents( parent_counter );

	const auto current_siblings_name { std::format( "current_tag_siblings_{}", id ) };
	client_tr << std::format( "SELECT * FROM {}", current_siblings_name ) >>
		[ & ]( const int bad_id, const int good_id )
	{
		sibling_counter += 1;
		if ( sibling_counter % 64'000 ) emit processedMaxAliases( sibling_counter );
	};

	emit processedMaxAliases( sibling_counter );

	m_preprocessed = true;

	emit finished();
}

void TagServiceWorker::processSets( const std::vector< MappingPair >& pairs ) const
{
	FGL_ASSERT( m_importer, "Importer was null!" );
	idhan::hydrus::TransactionBase master_tr { m_importer->master_db };

	using HyHashID = int;
	using HyTagID = int;
	using TagPair = std::pair< std::string, std::string >;

	std::unordered_map< HyHashID, std::vector< HyTagID > > tag_map {};
	tag_map.reserve( pairs.size() );
	std::unordered_map< HyTagID, TagPair > tag_id_map {};
	tag_id_map.reserve( pairs.size() );

	std::unordered_set< HyHashID > hash_ids {};
	hash_ids.reserve( pairs.size() );
	std::unordered_map< HyHashID, std::vector< HyTagID > > hash_id_tag_id_map {};
	hash_id_tag_id_map.reserve( pairs.size() );
	std::unordered_map< HyTagID, TagPair > tag_id_pairs {};
	tag_id_pairs.reserve( pairs.size() );

	auto& client = idhan::IDHANClient::instance();

	// process tag ids
	for ( const auto& [ hash_id, tag_id ] : pairs )
	{
		hash_ids.emplace( hash_id );

		if ( !hash_id_tag_id_map.contains( hash_id ) )
		{
			hash_id_tag_id_map[ hash_id ] = {};
		}

		hash_id_tag_id_map[ hash_id ].emplace_back( tag_id );

		if ( !tag_id_pairs.contains( tag_id ) )
		{
			TagPair tag_pair {};

			idhan::hydrus::TransactionBaseCoro master_tr_coro { m_importer->master_db };
			idhan::hydrus::Query< std::string_view, std::string_view > query {
				master_tr_coro,
				"SELECT namespace, subtag FROM tags NATURAL JOIN namespaces NATURAL JOIN subtags WHERE tag_id = $1",
				tag_id
			};

			for ( const auto& [ namespace_i, subtag_i ] : query )
			{
				tag_pair = std::make_pair( namespace_i, subtag_i );
				tag_id_pairs[ tag_id ] = tag_pair;
			}

			tag_id_map[ tag_id ] = tag_pair;
		}
	}

	std::vector< std::string > hashes {};
	hashes.reserve( hash_ids.size() );
	std::vector< std::vector< TagPair > > tag_sets {};
	tag_sets.reserve( hash_ids.size() );

	for ( const auto& hash_id : hash_ids )
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
		tag_pairs.reserve( hash_id_tag_id_map[ hash_id ].size() );
		for ( const auto& tag_id : hash_id_tag_id_map[ hash_id ] )
		{
			tag_pairs.emplace_back( tag_id_map[ tag_id ] );
		}

		tag_sets.emplace_back( std::move( tag_pairs ) );
	}

	// Get unique tag ids

	auto record_future = client.createRecords( hashes );
	record_future.waitForFinished();

	auto future = client.addTags( std::move( record_future.result() ), tag_domain_id, std::move( tag_sets ) );
	future.waitForFinished();
}

void TagServiceWorker::importMappings()
{
	const auto id { m_service.service_id };
	const auto current_mappings_name { std::format( "current_mappings_{}", id ) };

	idhan::hydrus::TransactionBaseCoro mappings_tr { m_importer->mappings_db };

	auto& client = idhan::IDHANClient::instance();

	std::string str { m_service.name.toStdString() };

	auto tag_domain_f { client.createTagDomain( str ) };

	//TODO: Add handling for conflicting tag domains
	try
	{
		tag_domain_f.waitForFinished();
	}
	catch ( std::exception& e )
	{
		tag_domain_f = client.getTagDomain( str );
		tag_domain_f.waitForFinished();
	}

	tag_domain_id = tag_domain_f.result();

	processMappingsBatch( mappings_tr, current_mappings_name );

	emit processedParents( m_service.num_parents );
	emit processedAliases( m_service.num_aliases );

	emit finished();
}

void TagServiceWorker::run()
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
	}
}

void TagServiceWorker::processMappingsBatch(
	const idhan::hydrus::TransactionBaseCoro& mappings_tr, const std::string& current_mappings_name )
{
	std::vector< MappingPair > sets {};
	constexpr std::size_t hash_limit { 1000 }; // the bulk record insert can only do 100 per, So we'll buffer it to 10
	constexpr std::size_t average_tags_per_hash { 512 * 1024 };
	constexpr std::size_t set_limit { average_tags_per_hash * hash_limit };

	idhan::hydrus::Query< int, int > query {
		mappings_tr, std::format( "SELECT tag_id, hash_id FROM {} ORDER BY hash_id, tag_id", current_mappings_name )
	};

	std::size_t mappings_counter { 0 };

	std::unordered_set< int > hash_id_set {};

	for ( const auto& [ tag_id, hash_id ] : query )
	{
		// dump before processing the next item if we are over the limit
		if ( hash_id_set.size() >= hash_limit )
		{
			hash_id_set.clear();
			processSets( sets );
			sets.clear();
			sets.reserve( set_limit );
			emit processedMappings( mappings_counter );
			mappings_counter = 0;
		}

		mappings_counter += 1;
		sets.emplace_back( hash_id, tag_id );
		hash_id_set.insert( hash_id );
	}

	emit processedMappings( mappings_counter );
}
