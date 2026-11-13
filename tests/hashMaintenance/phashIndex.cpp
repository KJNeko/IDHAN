#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "hamming/PhashIndex.hpp"

namespace idhan::test
{
using hamming::HammingPair;
using hamming::PhashIndex;

TEST_CASE( "Phash index pairs probes with every record within the distance", "[phash][index]" )
{
	const PhashIndex index { { 1, 2, 3, 4 }, { 0x0, 0x1, 0xFF, 0x1FF } };

	CHECK( index.pairsWithin( std::vector< RecordID > { 1 }, 1 ) == std::vector< HammingPair > { { 1, 2, 1 } } );
	CHECK(
		index.pairsWithin( std::vector< RecordID > { 1 }, 8 )
		== std::vector< HammingPair > { { 1, 2, 1 }, { 1, 3, 8 } } );
	CHECK( index.pairsWithin( std::vector< RecordID > { 4 }, 0 ).empty() );
}

TEST_CASE( "Phash index reports each pair once when both ends are probes", "[phash][index]" )
{
	const PhashIndex index { { 5, 9 }, { 0xF0, 0xF0 } };

	CHECK( index.pairsWithin( std::vector< RecordID > { 9, 5 }, 8 ) == std::vector< HammingPair > { { 5, 9, 0 } } );
}

TEST_CASE( "Phash index skips probes it holds no hash for", "[phash][index]" )
{
	const PhashIndex index { { 1 }, { 0x0 } };

	CHECK( index.pairsWithin( std::vector< RecordID > { 2 }, 64 ).empty() );
}

TEST_CASE( "Phash index assignment replaces, inserts, and removes", "[phash][index]" )
{
	PhashIndex index { { 10, 30 }, { 0x0, 0x0 } };

	index.assign( 20, 0x3 );
	REQUIRE( index.size() == 3 );
	CHECK( index.find( 20 ) == 0x3 );
	CHECK( index.pairsWithin( std::vector< RecordID > { 20 }, 2 ) == std::vector< HammingPair > { { 10, 20, 2 }, { 20, 30, 2 } } );

	index.assign( 10, 0xFFFF );
	CHECK( index.find( 10 ) == 0xFFFF );
	CHECK( index.pairsWithin( std::vector< RecordID > { 20 }, 2 ) == std::vector< HammingPair > { { 20, 30, 2 } } );

	index.assign( 30, std::nullopt );
	CHECK_FALSE( index.find( 30 ).has_value() );
	CHECK( index.size() == 2 );

	index.assign( 40, std::nullopt );
	CHECK( index.size() == 2 );
}

TEST_CASE( "Phash index rejects unsorted or mismatched arrays", "[phash][index]" )
{
	CHECK_THROWS( PhashIndex { { 2, 1 }, { 0x0, 0x0 } } );
	CHECK_THROWS( PhashIndex { { 1, 2 }, { 0x0 } } );
}
} // namespace idhan::test
