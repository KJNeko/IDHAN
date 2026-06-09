#include <gtest/gtest.h>

#include <queue>
#include <random>
#include <set>
#include <unordered_set>

#include "db/fixtures/MappingFixture.hpp"
#include "logging/format_ns.hpp"

static constexpr char genRandomChar( const std::uint8_t value )
{
	const std::uint8_t v { value % 62 };
	if ( v < 26 ) return static_cast< char >( 'a' + v );
	if ( v < 52 ) return static_cast< char >( 'A' + ( v - 26 ) );
	return static_cast< char >( '0' + ( v - 52 ) );
}

static std::string randomString( std::mt19937& gen, std::size_t length )
{
	std::uniform_int_distribution< std::uint8_t > dist { 0, 61 };
	std::string result;
	result.resize( length );
	for ( std::size_t i = 0; i < length; ++i )
		result[ i ] = genRandomChar( dist( gen ) );
	return result;
}

class RelationshipFuzzer : public MappingFixture
{
  protected:

	std::mt19937 m_gen;

	RelationshipFuzzer()
	{
		auto seed = static_cast< std::mt19937::result_type >( ::testing::UnitTest::GetInstance()->random_seed() );
		if ( seed == 0 ) seed = std::random_device {}();
		m_gen.seed( seed );
	}

	// Create a random tag with a unique name
	TagID randomTag()
	{
		return createTag( format_ns::format( "fuzz:{}", randomString( m_gen, 12 ) ) );
	}

	// Pick a random element from a vector
	template < typename T >
	const T& randomChoice( const std::vector< T >& vec )
	{
		std::uniform_int_distribution< std::size_t > dist { 0, vec.size() - 1 };
		return vec[ dist( m_gen ) ];
	}

	void createSibling( TagID older_id, TagID younger_id )
	{
		pqxx::work tx { *conn };
		tx.exec_params(
			"INSERT INTO tag_siblings (older_id, younger_id, tag_domain_id) VALUES ($1, $2, $3)",
			pqxx::params { older_id, younger_id, default_domain_id } );
		tx.commit();
	}
};

// Fuzzer 1: Random alias chains with consistency verification
TEST_F( RelationshipFuzzer, RandomAliasChains )
{
	constexpr int NUM_TAGS { 50 };
	constexpr int NUM_ALIASES { 80 };

	std::vector< TagID > tags;
	tags.reserve( NUM_TAGS );
	for ( int i = 0; i < NUM_TAGS; ++i )
		tags.push_back( randomTag() );

	// Create random alias pairs, avoiding self-aliases
	std::uniform_int_distribution< std::size_t > tag_choice { 0, tags.size() - 1 };
	for ( int i = 0; i < NUM_ALIASES; ++i )
	{
		TagID aliased { tags[ tag_choice( m_gen ) ] };
		TagID alias { tags[ tag_choice( m_gen ) ] };

		if ( aliased == alias ) continue;

		try
		{
			createAlias( aliased, alias );
		}
		catch ( ... )
		{
			// Cycle rejection is fine, skip
		}
	}

	// Verify no cycles exist by ensuring every tag resolves to a fixed point
	for ( const auto& tag : tags )
	{
		const auto ideal { getIdealAliasId( tag ) };
		if ( ideal == 0 ) continue;

		// The ideal should never be the original tag (that would be a self-cycle)
		ASSERT_NE( ideal, tag );

		// The ideal should itself have no further ideal (it's the terminal)
		const auto ideal_of_ideal { getIdealAliasId( ideal ) };
		ASSERT_EQ( ideal_of_ideal, 0 );
	}
}

// Fuzzer 2: Random parent DAG with propagation verification
TEST_F( RelationshipFuzzer, RandomParentPropagation )
{
	constexpr int NUM_TAGS { 30 };
	constexpr int NUM_PARENTS { 40 };
	constexpr int NUM_RECORDS { 10 };
	constexpr int TAGS_PER_RECORD { 5 };

	std::vector< TagID > tags;
	tags.reserve( NUM_TAGS );
	for ( int i = 0; i < NUM_TAGS; ++i )
		tags.push_back( randomTag() );

	// Create random parent-child relationships
	std::uniform_int_distribution< std::size_t > tag_choice { 0, tags.size() - 1 };
	for ( int i = 0; i < NUM_PARENTS; ++i )
	{
		TagID parent { tags[ tag_choice( m_gen ) ] };
		TagID child { tags[ tag_choice( m_gen ) ] };

		if ( parent == child ) continue;

		try
		{
			createParent( parent, child );
		}
		catch ( ... )
		{
			continue;
		}
	}

	// Build an adjacency list from the DB for verification
	std::unordered_map< TagID, std::vector< TagID > > parent_to_children;
	{
		pqxx::work tx { *conn };
		const auto rows { tx.exec_params(
			"SELECT parent_id, child_id FROM tag_parents WHERE tag_domain_id = $1",
			pqxx::params { default_domain_id } ) };
		for ( const auto& row : rows )
		{
			const auto p { row[ 0 ].as< TagID >() };
			const auto c { row[ 1 ].as< TagID >() };
			parent_to_children[ p ].push_back( c );
		}
	}

	// Verify no direct self-parent
	for ( const auto& [ p, children ] : parent_to_children )
	{
		for ( const auto& c : children )
			ASSERT_NE( p, c );
	}

	// Create records with random tag subsets and verify propagation
	for ( int r = 0; r < NUM_RECORDS; ++r )
	{
		const auto record { createRecord( format_ns::format( "fuzz_record_{}", r ) ) };

		std::uniform_int_distribution< std::size_t > count_dist { 1, TAGS_PER_RECORD };
		const int num_tags_for_record { static_cast< int >( count_dist( m_gen ) ) };

		for ( int t = 0; t < num_tags_for_record; ++t )
		{
			const auto tag { tags[ tag_choice( m_gen ) ] };
			try
			{
				createMapping( tag, record );
			}
			catch ( ... )
			{
				continue;
			}
		}

		// Verify: every tag in parent_to_children that is also in active_tag_mappings
		// should have its parent in active_tag_mappings_parents
		pqxx::work tx { *conn };
		for ( const auto& [ p, children ] : parent_to_children )
		{
			for ( const auto& c : children )
			{
				// Skip the ON CONFLICT DO NOTHING path by checking existence
				const auto child_mapped { tx.exec_params(
					"SELECT EXISTS(SELECT 1 FROM tag_mappings WHERE tag_id = $1 AND record_id = $2 AND tag_domain_id = $3)",
					pqxx::params { c, record, default_domain_id } ) };

				if ( !child_mapped[ 0 ][ 0 ].as< bool >() ) continue;

				// Child is mapped; parent should be in active_tag_mappings_parents
				const auto parent_entry { tx.exec_params(
					"SELECT EXISTS(SELECT 1 FROM active_tag_mappings_parents WHERE record_id = $1 AND tag_id = $2 AND origin_id = $3 AND tag_domain_id = $4)",
					pqxx::params { record, p, c, default_domain_id } ) };

				// The parent should exist since the child is mapped
				ASSERT_TRUE( parent_entry[ 0 ][ 0 ].as< bool >() );
			}
		}
	}
}

// Fuzzer 3: Alias-parent-sibling interaction fuzzer
TEST_F( RelationshipFuzzer, RandomAliasParentInteraction )
{
	constexpr int NUM_TAGS { 40 };
	constexpr int NUM_ALIASES { 30 };
	constexpr int NUM_PARENTS { 30 };
	constexpr int NUM_SIBLINGS { 20 };
	constexpr int NUM_RECORDS { 8 };

	std::vector< TagID > tags;
	tags.reserve( NUM_TAGS );
	for ( int i = 0; i < NUM_TAGS; ++i )
		tags.push_back( randomTag() );

	std::uniform_int_distribution< std::size_t > tag_choice { 0, tags.size() - 1 };

	// Create random aliases
	for ( int i = 0; i < NUM_ALIASES; ++i )
	{
		const auto aliased { tags[ tag_choice( m_gen ) ] };
		const auto alias { tags[ tag_choice( m_gen ) ] };
		if ( aliased == alias ) continue;
		try { createAlias( aliased, alias ); }
		catch ( ... ) {}
	}

	// Create random parents
	for ( int i = 0; i < NUM_PARENTS; ++i )
	{
		const auto parent { tags[ tag_choice( m_gen ) ] };
		const auto child { tags[ tag_choice( m_gen ) ] };
		if ( parent == child ) continue;
		try { createParent( parent, child ); }
		catch ( ... ) {}
	}

	// Create random siblings
	for ( int i = 0; i < NUM_SIBLINGS; ++i )
	{
		const auto older { tags[ tag_choice( m_gen ) ] };
		const auto younger { tags[ tag_choice( m_gen ) ] };
		if ( older == younger ) continue;
		try
		{
			createSibling( older, younger );
		}
		catch ( ... )
		{}
	}

	// Collect all aliases from DB
	std::vector< std::pair< TagID, TagID > > aliases;
	{
		pqxx::work tx { *conn };
		const auto rows { tx.exec_params(
			"SELECT aliased_id, alias_id FROM tag_aliases WHERE tag_domain_id = $1",
			pqxx::params { default_domain_id } ) };
		for ( const auto& row : rows )
			aliases.emplace_back( row[ 0 ].as< TagID >(), row[ 1 ].as< TagID >() );
	}

	// Collect all parents from DB
	std::vector< std::pair< TagID, TagID > > parents;
	{
		pqxx::work tx { *conn };
		const auto rows { tx.exec_params(
			"SELECT parent_id, child_id FROM tag_parents WHERE tag_domain_id = $1",
			pqxx::params { default_domain_id } ) };
		for ( const auto& row : rows )
			parents.emplace_back( row[ 0 ].as< TagID >(), row[ 1 ].as< TagID >() );
	}

	// Collect sibling pairs from aliased_siblings for silencing verification
	struct SiblingPair
	{
		TagID older;
		TagID younger;
	};

	std::vector< SiblingPair > sibling_pairs;
	{
		pqxx::work tx { *conn };
		auto rows { tx.exec_params(
			"SELECT older_id, younger_id FROM aliased_siblings WHERE tag_domain_id = $1",
			pqxx::params { default_domain_id } ) };
		for ( const auto& row : rows ) sibling_pairs.push_back( { row[ 0 ].as< TagID >(), row[ 1 ].as< TagID >() } );
	}

	// Create records with random tags and verify the resulting state
	for ( int r = 0; r < NUM_RECORDS; ++r )
	{
		const auto record { createRecord( format_ns::format( "fuzz_ap_record_{}", r ) ) };

		std::uniform_int_distribution< int > count_dist { 1, 8 };
		const int num_tags { count_dist( m_gen ) };
		std::unordered_set< TagID > mapped;
		for ( int t = 0; t < num_tags; ++t )
		{
			const auto tag { tags[ tag_choice( m_gen ) ] };
			if ( mapped.count( tag ) ) continue;
			try
			{
				createMapping( tag, record );
				mapped.insert( tag );
			}
			catch ( ... )
			{}
		}

		// Verify silencing: if both older and younger of a sibling pair are mapped,
		// the younger must be silenced
		{
			pqxx::work tx { *conn };
			for ( const auto& [ older, younger ] : sibling_pairs )
			{
				if ( mapped.count( older ) == 0 || mapped.count( younger ) == 0 ) continue;

				const auto silenced { tx.exec_params(
					"SELECT silenced FROM active_tag_mappings WHERE record_id = $1 AND tag_id = $2 AND tag_domain_id = $3",
					pqxx::params { record, younger, default_domain_id } ) };
				ASSERT_FALSE( silenced.empty() );
				ASSERT_TRUE( silenced[ 0 ][ 0 ].as< bool >() )
					<< "Younger " << younger << " should be silenced by older " << older;
			}
		}

		// Count total active mappings (including parent-propagation)
		pqxx::work tx_sil { *conn };
		const auto mapping_count { tx_sil.exec_params(
			"SELECT COUNT(*) FROM active_tag_mappings WHERE record_id = $1 AND tag_domain_id = $2",
			pqxx::params { record, default_domain_id } ) };
		const auto parent_mapping_count { tx_sil.exec_params(
			"SELECT COUNT(*) FROM active_tag_mappings_parents WHERE record_id = $1 AND tag_domain_id = $2",
			pqxx::params { record, default_domain_id } ) };

		// Both counts should be non-negative (trivially true)
		const auto m_count { mapping_count[ 0 ][ 0 ].as< std::size_t >() };
		const auto pm_count { parent_mapping_count[ 0 ][ 0 ].as< std::size_t >() };
		ASSERT_GE( m_count, 0 );
		ASSERT_GE( pm_count, 0 );

		// The direct mapping count should be >= 1 (we created at least one)
		ASSERT_GE( m_count, 1 );

		// Verify that the active_tag_mappings_final view works:
		// final = active_tag_mappings (not silenced) + active_tag_mappings_parents (all)
		const auto silenced_count { tx_sil.exec_params(
			"SELECT COUNT(*) FROM active_tag_mappings WHERE record_id = $1 AND tag_domain_id = $2 AND silenced",
			pqxx::params { record, default_domain_id } ) };
		const auto final_count { tx_sil.exec_params(
			"SELECT COUNT(*) FROM active_tag_mappings_final WHERE record_id = $1 AND tag_domain_id = $2",
			pqxx::params { record, default_domain_id } ) };
		ASSERT_EQ(
			final_count[ 0 ][ 0 ].as< std::size_t >(),
			m_count - silenced_count[ 0 ][ 0 ].as< std::size_t >() + pm_count );
	}
}

// Fuzzer 4: Stress test with large batch
TEST_F( RelationshipFuzzer, StressTestLargeBatch )
{
	constexpr int NUM_TAGS { 100 };
	constexpr int NUM_ALIASES { 50 };
	constexpr int NUM_PARENTS { 50 };
	constexpr int NUM_SIBLINGS { 30 };
	constexpr int NUM_RECORDS { 20 };

	std::vector< TagID > tags;
	tags.reserve( NUM_TAGS );
	for ( int i = 0; i < NUM_TAGS; ++i )
		tags.push_back( randomTag() );

	std::uniform_int_distribution< std::size_t > tag_choice { 0, tags.size() - 1 };

	// Create aliases
	for ( int i = 0; i < NUM_ALIASES; ++i )
	{
		const auto a { tags[ tag_choice( m_gen ) ] };
		const auto b { tags[ tag_choice( m_gen ) ] };
		if ( a == b ) continue;
		try { createAlias( a, b ); } catch ( ... ) {}
	}

	// Create parents
	for ( int i = 0; i < NUM_PARENTS; ++i )
	{
		const auto p { tags[ tag_choice( m_gen ) ] };
		const auto c { tags[ tag_choice( m_gen ) ] };
		if ( p == c ) continue;
		try { createParent( p, c ); } catch ( ... )
		{}
	}

	// Create siblings
	for ( int i = 0; i < NUM_SIBLINGS; ++i )
	{
		const auto older { tags[ tag_choice( m_gen ) ] };
		const auto younger { tags[ tag_choice( m_gen ) ] };
		if ( older == younger ) continue;
		try
		{
			createSibling( older, younger );
		}
		catch ( ... )
		{}
	}

	// Query aliased_siblings for silencing verification
	struct SiblingPair
	{
		TagID older;
		TagID younger;
	};

	std::vector< SiblingPair > sibling_pairs;
	{
		pqxx::work tx { *conn };
		auto rows { tx.exec_params(
			"SELECT older_id, younger_id FROM aliased_siblings WHERE tag_domain_id = $1",
			pqxx::params { default_domain_id } ) };
		for ( const auto& row : rows ) sibling_pairs.push_back( { row[ 0 ].as< TagID >(), row[ 1 ].as< TagID >() } );
	}

	// Map each record with ~10 tags
	std::vector< RecordID > records;
	records.reserve( NUM_RECORDS );
	for ( int r = 0; r < NUM_RECORDS; ++r )
	{
		const auto record { createRecord( format_ns::format( "stress_record_{}", r ) ) };
		records.push_back( record );

		// Track which tags are mapped for silencing verification
		std::unordered_set< TagID > mapped;
		for ( int t = 0; t < 10; ++t )
		{
			const auto tag { tags[ tag_choice( m_gen ) ] };
			if ( mapped.count( tag ) ) continue;
			try
			{
				createMapping( tag, record );
				mapped.insert( tag );
			}
			catch ( ... )
			{}
		}

		// Verify silencing: if both older and younger of a sibling pair are mapped,
		// the younger must be silenced
		{
			pqxx::work tx { *conn };
			for ( const auto& [ older, younger ] : sibling_pairs )
			{
				if ( mapped.count( older ) == 0 || mapped.count( younger ) == 0 ) continue;

				const auto silenced { tx.exec_params(
					"SELECT silenced FROM active_tag_mappings WHERE record_id = $1 AND tag_id = $2 AND tag_domain_id = $3",
					pqxx::params { record, younger, default_domain_id } ) };
				ASSERT_FALSE( silenced.empty() );
				ASSERT_TRUE( silenced[ 0 ][ 0 ].as< bool >() )
					<< "Younger " << younger << " should be silenced by older " << older;
			}
		}
	}

	// Verify tag counts are consistent
	pqxx::work tx_counts { *conn };
	for ( const auto& tag : tags )
	{
		const auto storage_count { tx_counts.exec_params(
			"SELECT storage_count FROM tag_counts WHERE tag_id = $1",
			pqxx::params { tag } ) };

		if ( storage_count.empty() ) continue;

		const auto actual_count { tx_counts.exec_params(
			"SELECT COUNT(*) FROM tag_mappings WHERE tag_id = $1 AND tag_domain_id = $2",
			pqxx::params { tag, default_domain_id } ) };

		const auto expected { actual_count[ 0 ][ 0 ].as< std::size_t >() };
		const auto stored { storage_count[ 0 ][ 0 ].as< std::size_t >() };
		ASSERT_EQ( stored, expected );
	}
}

// Fuzzer 5: Build a C++ local model of aliases, parents, and siblings from PG state,
// then independently compute expected parent propagation rows and compare
TEST_F( RelationshipFuzzer, LocalModelConsistency )
{
	constexpr int NUM_TAGS { 30 };
	constexpr int NUM_ALIASES { 40 };
	constexpr int NUM_PARENTS { 25 };
	constexpr int NUM_SIBLINGS { 20 };
	constexpr int NUM_RECORDS { 5 };
	constexpr int TAGS_PER_RECORD { 6 };

	std::vector< TagID > tags;
	tags.reserve( NUM_TAGS );
	for ( int i = 0; i < NUM_TAGS; ++i )
		tags.push_back( randomTag() );

	std::uniform_int_distribution< std::size_t > tag_choice { 0, tags.size() - 1 };

	// Create random aliases
	for ( int i = 0; i < NUM_ALIASES; ++i )
	{
		const auto aliased { tags[ tag_choice( m_gen ) ] };
		const auto alias { tags[ tag_choice( m_gen ) ] };
		if ( aliased == alias ) continue;
		try { createAlias( aliased, alias ); }
		catch ( ... ) {}
	}

	// Create random parents
	for ( int i = 0; i < NUM_PARENTS; ++i )
	{
		const auto parent { tags[ tag_choice( m_gen ) ] };
		const auto child { tags[ tag_choice( m_gen ) ] };
		if ( parent == child ) continue;
		try { createParent( parent, child );
		}
		catch ( ... )
		{}
	}

	// Create random siblings
	for ( int i = 0; i < NUM_SIBLINGS; ++i )
	{
		const auto older { tags[ tag_choice( m_gen ) ] };
		const auto younger { tags[ tag_choice( m_gen ) ] };
		if ( older == younger ) continue;
		try
		{
			createSibling( older, younger ); }
		catch ( ... ) {}
	}

	// Build C++ local model by reading PG's idealized state
	std::unordered_map< TagID, TagID > alias_ideal;  // aliased → effective_tag_id
	std::unordered_map< TagID, std::vector< TagID > > raw_parent_edges;   // raw child_id → [raw parent_id]
	std::unordered_map< TagID, std::vector< TagID > > ideal_parent_edges; // COALESCE(ideal_child, child) → [COALESCE(ideal_parent, parent)]
	std::unordered_map< TagID, TagID > older_sibling;  // younger_id → oldest older_id
	{
		pqxx::work tx { *conn };
		auto rows { tx.exec_params(
			"SELECT aliased_id, effective_tag_id FROM tag_aliases WHERE tag_domain_id = $1",
			pqxx::params { default_domain_id } ) };
		for ( const auto& row : rows )
			alias_ideal[ row[ 0 ].as< TagID >() ] = row[ 1 ].as< TagID >();

		// Raw parent edges: matches insert_parents_from_active_mappings (Part 1 & Part 2)
		// which uses tp.child_id (raw) and tp.parent_id (raw)
		rows = tx.exec_params(
			"SELECT child_id, parent_id FROM tag_parents WHERE tag_domain_id = $1",
			pqxx::params { default_domain_id } );
		for ( const auto& row : rows )
		{
			const auto child { row[ 0 ].as< TagID >() };
			const auto parent { row[ 1 ].as< TagID >() };
			raw_parent_edges[ child ].push_back( parent );
		}

		// Idealized parent edges: matches atmp_internal_on_insert
		// which uses COALESCE(ideal_child_id, child_id) and COALESCE(ideal_parent_id, parent_id)
		rows = tx.exec_params(
			"SELECT COALESCE(ideal_child_id, child_id), COALESCE(ideal_parent_id, parent_id) FROM tag_parents WHERE tag_domain_id = $1",
			pqxx::params { default_domain_id } );
		for ( const auto& row : rows )
		{
			const auto child { row[ 0 ].as< TagID >() };
			const auto parent { row[ 1 ].as< TagID >() };
			ideal_parent_edges[ child ].push_back( parent );
		}

		// Sibling pairs matching the ATMP BEFORE INSERT trigger (migration 170):
		// Looks up tag_siblings.younger_id (raw), replaces older with alias-resolved version
		rows = tx.exec_params(
			"SELECT ts.younger_id, COALESCE(ta.effective_tag_id, COALESCE(ts.ideal_older_id, ts.older_id)) "
			"FROM tag_siblings ts "
			"LEFT JOIN tag_aliases ta ON ta.aliased_id = COALESCE(ts.ideal_older_id, ts.older_id) "
			"  AND ta.tag_domain_id = ts.tag_domain_id "
			"WHERE ts.tag_domain_id = $1",
			pqxx::params { default_domain_id } );
		for ( const auto& row : rows )
		{
			const auto younger { row[ 0 ].as< TagID >() };
			const auto older { row[ 1 ].as< TagID >() };
			older_sibling[ younger ] = older;
		}
	}

	// Resolve ideal tag by following alias chain
	// Returns the terminal effective_tag_id for aliased tags, or 0 if no alias exists
	auto resolveIdeal = [&]( TagID tag ) -> TagID
	{
		auto it = alias_ideal.find( tag );
		if ( it == alias_ideal.end() ) return 0;
		std::unordered_set< TagID > seen;
		seen.insert( tag );
		while ( true )
		{
			tag = it->second;
			it = alias_ideal.find( tag );
			if ( it == alias_ideal.end() ) return tag;
			if ( !seen.insert( tag ).second ) return tag;
		}
	};

	// Resolve sibling: return the effective older for a tag, or the tag itself
	// The ATMP BEFORE INSERT trigger (migration 170) does a single-level lookup:
	//   COALESCE(ta.effective_tag_id, COALESCE(ts.ideal_older_id, ts.older_id))
	// The ideal_older_id column (set by trg_tag_siblings_after_insert) already
	// encodes the TERMINAL older of the transitive chain, so no recursive following
	// is needed. Following beyond the first level would walk through intermediate
	// tags that have their own ideal_older_id, potentially creating cycles.
	auto resolveSibling = [ & ]( TagID tag ) -> TagID
	{
		auto it = older_sibling.find( tag );
		if ( it == older_sibling.end() ) return tag;
		return it->second;
	};

	// Helper: compute expected ATMP rows for parents of a child tag
	// Uses raw_edges for the first level (Part 1/2 trigger) and ideal_edges for internal chaining
	// Each parent tag_id is resolved through siblings (ATMP BEFORE INSERT trigger replaces with oldest sibling)
	auto computeExpectedATMP = [&]( TagID child ) -> std::vector< std::pair< TagID, TagID > >
	{
		std::vector< std::pair< TagID, TagID > > result;

		// First level: raw parent edges (matches insert_parents_from_active_mappings)
		auto raw_it = raw_parent_edges.find( child );
		if ( raw_it != raw_parent_edges.end() )
		{
			for ( const auto& parent : raw_it->second )
			{
				const auto resolved_parent { resolveSibling( parent ) };
				result.emplace_back( resolved_parent, child );

				// Internal chaining from resolved parent (matches atmp_internal_on_insert)
				// Use a visited set to prevent cycles (parent graph may contain cycles)
				std::queue< TagID > chain_q;
				std::unordered_set< TagID > visited { resolved_parent };
				chain_q.push( resolved_parent );
				while ( !chain_q.empty() )
				{
					const auto current { chain_q.front() };
					chain_q.pop();

					auto ideal_it = ideal_parent_edges.find( current );
					if ( ideal_it == ideal_parent_edges.end() ) continue;

					for ( const auto& grandparent : ideal_it->second )
					{
						const auto resolved_gp { resolveSibling( grandparent ) };
						result.emplace_back( resolved_gp, current );
						if ( visited.insert( resolved_gp ).second ) chain_q.push( resolved_gp );
					}
				}
			}
		}

		return result;
	};

	// Create records and map random tags
	for ( int r = 0; r < NUM_RECORDS; ++r )
	{
		const auto record { createRecord( format_ns::format( "lm_record_{}", r ) ) };

		std::uniform_int_distribution< int > count_dist { 1, TAGS_PER_RECORD };
		const int num_tags { count_dist( m_gen ) };

		std::unordered_set< TagID > mapped_tags;
		for ( int t = 0; t < num_tags; ++t )
		{
			const auto tag { tags[ tag_choice( m_gen ) ] };
			if ( mapped_tags.count( tag ) ) continue;
			try
			{
				createMapping( tag, record );
				mapped_tags.insert( tag );
			}
			catch ( ... ) {}
		}

		// Compute expected ATMP rows using the local C++ model
		// PG trigger insert_parents_from_active_mappings:
		//   Part 1: raw parent edges of new.tag_id (origin = new.tag_id)
		//   Part 2: raw parent edges of new.ideal_tag_id if different (origin = new.ideal_tag_id)
		// PG trigger atmp_internal_on_insert:
		//   Chains to grandparents using idealized parent edges
		std::vector< std::pair< TagID, TagID > > expected_atmp;  // (parent_id, origin_id)

		for ( const auto& tag : mapped_tags )
		{
			// Part 1: parents of the stored tag
			auto part1 = computeExpectedATMP( tag );
			expected_atmp.insert( expected_atmp.end(), part1.begin(), part1.end() );

			// Part 2: parents of the alias target (ideal), if different
			const auto ideal { resolveIdeal( tag ) };
			if ( ideal != 0 && ideal != tag )
			{
				auto part2 = computeExpectedATMP( ideal );
				expected_atmp.insert( expected_atmp.end(), part2.begin(), part2.end() );
			}
		}

		// Deduplicate: ATMP PK prevents (tag_id, origin_id) duplicates via ON CONFLICT
		std::set< std::pair< TagID, TagID > > deduped(
			expected_atmp.begin(), expected_atmp.end() );

		// Compare with actual PG state
		pqxx::work tx { *conn };

		// 1. Verify every expected ATMP row exists
		for ( const auto& [ parent_id, origin_id ] : deduped )
		{
			const auto exists { tx.exec_params(
				"SELECT EXISTS(SELECT 1 FROM active_tag_mappings_parents "
				"WHERE record_id = $1 AND tag_id = $2 AND origin_id = $3 AND tag_domain_id = $4)",
				pqxx::params { record, parent_id, origin_id, default_domain_id } ) };
			if ( !exists[ 0 ][ 0 ].as< bool >() )
			{
				ADD_FAILURE() << "Missing ATMP row: record=" << record << " parent=" << parent_id
							  << " origin=" << origin_id;
			}
		}

		// 2. Verify total ATMP row count matches expectation
		const auto actual_count { tx.exec_params(
			"SELECT COUNT(*) FROM active_tag_mappings_parents WHERE record_id = $1 AND tag_domain_id = $2",
			pqxx::params { record, default_domain_id } ) };
		ASSERT_EQ( actual_count[ 0 ][ 0 ].as< std::size_t >(), deduped.size() );

		// 3. Verify each active_tag_mappings tag has its ideal correctly resolved
		for ( const auto& tag : mapped_tags )
		{
			const auto expected_ideal { resolveIdeal( tag ) };
			const auto actual_ideal { tx.exec_params(
				"SELECT ideal_tag_id FROM active_tag_mappings WHERE record_id = $1 AND tag_id = $2 AND tag_domain_id = $3",
				pqxx::params { record, tag, default_domain_id } ) };
			if ( !actual_ideal.empty() && !actual_ideal[ 0 ][ 0 ].is_null() )
			{
				const auto actual_ideal_val { actual_ideal[ 0 ][ 0 ].as< TagID >() };
				ASSERT_EQ( actual_ideal_val, expected_ideal );
			}
			else if ( !actual_ideal.empty() && actual_ideal[ 0 ][ 0 ].is_null() )
			{
				ASSERT_EQ( 0, expected_ideal );
			}
		}

		// 4. Verify active_tag_mappings_final contains expected total rows
		// active_tag_mappings_final (migration 172) excludes silenced ATM rows via WHERE NOT silenced
		const auto atm_count { tx.exec_params(
			"SELECT COUNT(*) FROM active_tag_mappings WHERE record_id = $1 AND tag_domain_id = $2 AND NOT silenced",
			pqxx::params { record, default_domain_id } ) };
		const auto final_count { tx.exec_params(
			"SELECT COUNT(*) FROM active_tag_mappings_final WHERE record_id = $1 AND tag_domain_id = $2",
			pqxx::params { record, default_domain_id } ) };
		ASSERT_EQ(
			final_count[ 0 ][ 0 ].as< std::size_t >(),
			atm_count[ 0 ][ 0 ].as< std::size_t >() + deduped.size() );
	}
}

// Fuzzer 6: Random sibling silencing interactions
TEST_F( RelationshipFuzzer, RandomSiblingSilencing )
{
	constexpr int NUM_TAGS { 30 };
	constexpr int NUM_SIBLINGS { 25 };

	std::vector< TagID > tags;
	tags.reserve( NUM_TAGS );
	for ( int i = 0; i < NUM_TAGS; ++i ) tags.push_back( randomTag() );

	std::uniform_int_distribution< std::size_t > tag_choice { 0, tags.size() - 1 };

	// Create random sibling pairs
	for ( int i = 0; i < NUM_SIBLINGS; ++i )
	{
		const auto older { tags[ tag_choice( m_gen ) ] };
		const auto younger { tags[ tag_choice( m_gen ) ] };
		if ( older == younger ) continue;
		try
		{
			createSibling( older, younger );
		}
		catch ( ... )
		{}
	}

	// Collect all sibling pairs from aliased_siblings for verification
	struct SiblingPair
	{
		TagID older;
		TagID younger;
	};

	std::vector< SiblingPair > sibling_pairs;
	{
		pqxx::work tx { *conn };
		auto rows { tx.exec_params(
			"SELECT older_id, younger_id FROM aliased_siblings WHERE tag_domain_id = $1",
			pqxx::params { default_domain_id } ) };
		for ( const auto& row : rows ) sibling_pairs.push_back( { row[ 0 ].as< TagID >(), row[ 1 ].as< TagID >() } );
	}

	if ( sibling_pairs.empty() ) GTEST_SKIP();

	constexpr int NUM_RECORDS { 10 };
	constexpr int TAGS_PER_RECORD { 8 };
	std::uniform_int_distribution< int > count_dist { 1, TAGS_PER_RECORD };

	for ( int r = 0; r < NUM_RECORDS; ++r )
	{
		const auto record { createRecord( format_ns::format( "sib_fuzz_{}", r ) ) };
		const int num_tags { count_dist( m_gen ) };

		std::unordered_set< TagID > mapped;
		for ( int t = 0; t < num_tags; ++t )
		{
			const auto tag { tags[ tag_choice( m_gen ) ] };
			if ( mapped.count( tag ) ) continue;
			mapped.insert( tag );
			try
			{
				createMapping( tag, record );
			}
			catch ( ... )
			{}
		}

		// Verify silencing for every sibling pair
		{
			pqxx::work tx { *conn };
			for ( const auto& [ older, younger ] : sibling_pairs )
			{
				const bool older_mapped = mapped.count( older );
				const bool younger_mapped = mapped.count( younger );
				if ( !older_mapped || !younger_mapped ) continue;

				const auto row { tx.exec_params(
					"SELECT silenced FROM active_tag_mappings WHERE record_id = $1 AND tag_id = $2 AND tag_domain_id = $3",
					pqxx::params { record, younger, default_domain_id } ) };
				ASSERT_FALSE( row.empty() );
				ASSERT_TRUE( row[ 0 ][ 0 ].as< bool >() )
					<< "Younger " << younger << " (domain " << default_domain_id << ") should be silenced by older "
					<< older;
			}
		}

		// Verify unsilencing: remove the older tag's mapping, younger should unsilence
		for ( const auto& [ older, younger ] : sibling_pairs )
		{
			if ( mapped.count( older ) == 0 || mapped.count( younger ) == 0 ) continue;

			// Remove older mapping
			{
				pqxx::work tx_del { *conn };
				tx_del.exec_params(
					"DELETE FROM tag_mappings WHERE tag_id = $1 AND record_id = $2", pqxx::params { older, record } );
				tx_del.commit();
			}
			mapped.erase( older );

			// Verify younger is no longer silenced (unless silenced by another sibling)
			pqxx::work tx_vfy { *conn };
			const auto row { tx_vfy.exec_params(
				"SELECT silenced FROM active_tag_mappings WHERE record_id = $1 AND tag_id = $2 AND tag_domain_id = $3",
				pqxx::params { record, younger, default_domain_id } ) };
			tx_vfy.commit();
			ASSERT_FALSE( row.empty() );

			// Check if any OTHER mapped sibling pair also silences younger
			bool still_silenced_by_other = false;
			for ( const auto& [ other_older, other_younger ] : sibling_pairs )
			{
				if ( other_older == older && other_younger == younger ) continue;
				if ( other_younger != younger ) continue;
				if ( mapped.count( other_older ) )
				{
					still_silenced_by_other = true;
					break;
				}
			}
			ASSERT_EQ( row[ 0 ][ 0 ].as< bool >(), still_silenced_by_other );
		}

		// Verify storage_count and display_count
		pqxx::work tx_counts { *conn };
		for ( const auto& tag : tags )
		{
			const auto counts { tx_counts.exec_params(
				"SELECT storage_count, display_count FROM tag_counts WHERE tag_id = $1 AND tag_domain_id = $2",
				pqxx::params { tag, default_domain_id } ) };
			if ( counts.empty() ) continue;

			const auto storage_count { counts[ 0 ][ 0 ].as< std::size_t >() };
			const auto display_count { counts[ 0 ][ 1 ].as< std::size_t >() };

			const auto actual_mappings { tx_counts.exec_params(
				"SELECT COUNT(*) FROM tag_mappings WHERE tag_id = $1 AND tag_domain_id = $2",
				pqxx::params { tag, default_domain_id } ) };
			const auto expected_storage { actual_mappings[ 0 ][ 0 ].as< std::size_t >() };
			ASSERT_EQ( storage_count, expected_storage );

			const auto actual_display { tx_counts.exec_params(
				"SELECT COUNT(*) FROM active_tag_mappings WHERE tag_id = $1 AND tag_domain_id = $2 AND silenced = FALSE",
				pqxx::params { tag, default_domain_id } ) };
			const auto expected_display { actual_display[ 0 ][ 0 ].as< std::size_t >() };
			ASSERT_EQ( display_count, expected_display );
		}
	}
}
