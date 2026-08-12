#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include "ptr/flatten/MappingEvent.hpp"

namespace
{

using namespace idhan::hydrus::ptr;

MappingEvent
	ev( const std::uint32_t hash_id, const std::uint32_t tag_id, const std::uint16_t update_index, const EventOp op )
{
	return MappingEvent { hash_id, tag_id, update_index, static_cast< std::uint8_t >( op ), 0 };
}

TEST( PTRMappingEvent, IsTwelveBytes )
{
	EXPECT_EQ( sizeof( MappingEvent ), 12u );
}

TEST( PTRMappingEvent, BucketIsStableForOneHash )
{
	// Every event for one hash_id must land in exactly one bucket -- this is the property the
	// whole partitioning scheme rests on.
	const auto bucket = bucketFor( 194'644'713u );
	for ( std::uint32_t tag_id = 0; tag_id < 1000; ++tag_id ) EXPECT_EQ( bucketFor( 194'644'713u ), bucket );
	EXPECT_LT( bucket, BUCKET_COUNT );
}

TEST( PTRMappingEvent, BucketCoversFullRange )
{
	EXPECT_EQ( bucketFor( 0u ), 0u );
	EXPECT_EQ( bucketFor( BUCKET_COUNT - 1 ), BUCKET_COUNT - 1 );
	EXPECT_EQ( bucketFor( BUCKET_COUNT ), 0u );
}

TEST( PTRMappingEvent, SortsByHashThenTagThenIndex )
{
	std::vector< MappingEvent > events {
		ev( 2, 1, 1, EventOp::Add ),
		ev( 1, 2, 1, EventOp::Add ),
		ev( 1, 1, 2, EventOp::Add ),
		ev( 1, 1, 1, EventOp::Add ),
	};

	std::ranges::sort( events, eventLess );

	EXPECT_EQ( events[ 0 ].hash_id, 1u );
	EXPECT_EQ( events[ 0 ].tag_id, 1u );
	EXPECT_EQ( events[ 0 ].update_index, 1u );
	EXPECT_EQ( events[ 1 ].update_index, 2u );
	EXPECT_EQ( events[ 2 ].tag_id, 2u );
	EXPECT_EQ( events[ 3 ].hash_id, 2u );
}

TEST( PTRMappingEvent, DeleteSortsAfterAddInSameUpdateIndex )
{
	std::vector< MappingEvent > events { ev( 1, 1, 5, EventOp::Delete ), ev( 1, 1, 5, EventOp::Add ) };

	std::ranges::sort( events, eventLess );

	EXPECT_EQ( events[ 0 ].op, static_cast< std::uint8_t >( EventOp::Add ) );
	EXPECT_EQ( events[ 1 ].op, static_cast< std::uint8_t >( EventOp::Delete ) );
}

} // namespace
