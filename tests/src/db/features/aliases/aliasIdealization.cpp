#include "db/fixtures/ServerTagFixture.hpp"
#include "logging/format_ns.hpp"

TEST_F( ServerTagFixture, IdealAliasChain )
{
	const auto tag_a { createTag( "alias:chain_a" ) };
	const auto tag_b { createTag( "alias:chain_b" ) };
	const auto tag_c { createTag( "alias:chain_c" ) };

	createAlias( tag_a, tag_b );
	createAlias( tag_b, tag_c );

	const auto ideal_a { getIdealAliasId( tag_a ) };
	const auto ideal_b { getIdealAliasId( tag_b ) };

	ASSERT_EQ( ideal_a, tag_c );
	ASSERT_EQ( ideal_b, tag_c );
}

TEST_F( ServerTagFixture, AliasChainFiveDeep )
{
	std::vector< TagID > tags;
	for ( int i = 0; i < 5; ++i ) tags.push_back( createTag( format_ns::format( "alias:deep_{}", i ) ) );

	for ( std::size_t i = 0; i < tags.size() - 1; ++i ) createAlias( tags[ i ], tags[ i + 1 ] );

	for ( std::size_t i = 0; i < tags.size() - 1; ++i ) ASSERT_EQ( getIdealAliasId( tags[ i ] ), tags.back() );
}

TEST_F( ServerTagFixture, SliceMiddleOfChain )
{
	const auto tag_a { createTag( "alias:slice_a" ) };
	const auto tag_b { createTag( "alias:slice_b" ) };
	const auto tag_c { createTag( "alias:slice_c" ) };

	createAlias( tag_a, tag_b );
	createAlias( tag_b, tag_c );

	ASSERT_EQ( getIdealAliasId( tag_a ), tag_c );
	ASSERT_EQ( getIdealAliasId( tag_b ), tag_c );
	ASSERT_EQ( getIdealAliasId( tag_c ), 0 );

	// Now delete B explicitly from tag_aliases
	removeAlias( tag_b );

	// A should now resolve to B (no more chaining from B to C)
	ASSERT_EQ( getIdealAliasId( tag_a ), tag_b );

	// B should now resolve to 0 (its alias row is gone)
	ASSERT_EQ( getIdealAliasId( tag_b ), 0 );

	// C should remain unaffected
	ASSERT_EQ( getIdealAliasId( tag_c ), 0 );
}

TEST_F( ServerTagFixture, SelfAliasFails )
{
	const auto tag { createTag( "alias:self" ) };

	ASSERT_ANY_THROW( createAlias( tag, tag ) );
}

TEST_F( ServerTagFixture, AliasReverseLookup )
{
	const auto tag_a { createTag( "alias:rev_a" ) };
	const auto tag_b { createTag( "alias:rev_b" ) };

	createAlias( tag_a, tag_b );

	pqxx::work tx { *conn };
	const auto result { tx.exec_params(
		"SELECT aliased_id FROM tag_aliases WHERE alias_id = $1 AND tag_domain_id = $2",
		pqxx::params { tag_b, default_domain_id } ) };

	ASSERT_FALSE( result.empty() );
	ASSERT_EQ( result[ 0 ][ 0 ].as< TagID >(), tag_a );
}

TEST_F( ServerTagFixture, AliasDoesNotExistReturnsZero )
{
	const auto tag { createTag( "alias:nonexistent" ) };

	const auto ideal { getIdealAliasId( tag ) };
	ASSERT_EQ( ideal, 0 );
}

TEST_F( ServerTagFixture, TerminalResolvesToZero )
{
	const auto tag_a { createTag( "alias:terminal_a" ) };
	const auto tag_b { createTag( "alias:terminal_b" ) };

	createAlias( tag_a, tag_b );

	// tag_b is the terminal — it should have no alias entry
	ASSERT_EQ( getIdealAliasId( tag_b ), 0 );

	// tag_a resolves to tag_b
	ASSERT_EQ( getIdealAliasId( tag_a ), tag_b );

	// tag_b has no ideal_alias_id row
	ASSERT_EQ( getIdealAliasIdRaw( tag_b ), 0 );

	// tag_a's effective_tag_id should be tag_b
	ASSERT_EQ( getAliasEffectiveTagId( tag_a ), tag_b );
}

TEST_F( ServerTagFixture, TwoAliasesToSameTarget )
{
	const auto tag_a { createTag( "alias:diamond_a" ) };
	const auto tag_b { createTag( "alias:diamond_b" ) };
	const auto tag_c { createTag( "alias:diamond_c" ) };

	createAlias( tag_a, tag_c );
	createAlias( tag_b, tag_c );

	// Both resolve to the same target
	ASSERT_EQ( getIdealAliasId( tag_a ), tag_c );
	ASSERT_EQ( getIdealAliasId( tag_b ), tag_c );

	// Target itself still resolves to 0
	ASSERT_EQ( getIdealAliasId( tag_c ), 0 );
}

TEST_F( ServerTagFixture, ThreeDeepCycleRejected )
{
	const auto tag_1 { createTag( "alias:cycle_1" ) };
	const auto tag_2 { createTag( "alias:cycle_2" ) };
	const auto tag_3 { createTag( "alias:cycle_3" ) };

	createAlias( tag_1, tag_2 );
	createAlias( tag_2, tag_3 );

	// 1→2→3 forms a chain, now try 3→1 creating a cycle
	ASSERT_ANY_THROW( createAlias( tag_3, tag_1 ) );

	// Verify the chain is still intact
	ASSERT_EQ( getIdealAliasId( tag_1 ), tag_3 );
	ASSERT_EQ( getIdealAliasId( tag_2 ), tag_3 );
	ASSERT_EQ( getIdealAliasId( tag_3 ), 0 );
}

TEST_F( ServerTagFixture, InsertionAutoResolvesChain )
{
	const auto tag_a { createTag( "alias:auto_a" ) };
	const auto tag_b { createTag( "alias:auto_b" ) };
	const auto tag_c { createTag( "alias:auto_c" ) };

	// First create B→C
	createAlias( tag_b, tag_c );

	// Now create A→B — A should auto-resolve to C
	createAlias( tag_a, tag_b );

	// A's effective_tag_id should be C directly
	ASSERT_EQ( getAliasEffectiveTagId( tag_a ), tag_c );
	ASSERT_EQ( getIdealAliasId( tag_a ), tag_c );

	// B still resolves to C
	ASSERT_EQ( getIdealAliasId( tag_b ), tag_c );
}
