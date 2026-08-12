#include <gtest/gtest.h>

#include <filesystem>
#include <string>

#include "ptr/flatten/BucketSpill.hpp"

namespace
{

using namespace idhan::hydrus::ptr;

//! Scratch directory removed on destruction, so a failing assertion cannot leak spill files.
class BucketSpillTest : public ::testing::Test
{
  protected:

	void SetUp() override
	{
		m_dir = std::filesystem::temp_directory_path() / "ptr-bucket-test";
		std::filesystem::remove_all( m_dir );
		std::filesystem::create_directories( m_dir );
	}

	void TearDown() override { std::filesystem::remove_all( m_dir ); }

	std::filesystem::path m_dir;
};

TEST_F( BucketSpillTest, RoundTripsASingleEvent )
{
	{
		BucketWriter writer { m_dir };
		writer.write( MappingEvent { 5, 11, 3, static_cast< std::uint8_t >( EventOp::Add ), 0 } );
		writer.flush();
		EXPECT_EQ( writer.written(), 1u );
	}

	const auto events = readBucket( bucketPath( m_dir, bucketFor( 5 ) ) );
	ASSERT_EQ( events.size(), 1u );
	EXPECT_EQ( events[ 0 ].hash_id, 5u );
	EXPECT_EQ( events[ 0 ].tag_id, 11u );
	EXPECT_EQ( events[ 0 ].update_index, 3u );
	EXPECT_EQ( events[ 0 ].op, static_cast< std::uint8_t >( EventOp::Add ) );
}

TEST_F( BucketSpillTest, EveryEventForOneHashLandsInOneBucket )
{
	constexpr std::uint32_t HASH_ID { 194'644'713 };
	constexpr std::uint32_t TAG_COUNT { 500 };

	{
		BucketWriter writer { m_dir };
		for ( std::uint32_t tag_id = 0; tag_id < TAG_COUNT; ++tag_id )
			writer.write( MappingEvent { HASH_ID, tag_id, 1, static_cast< std::uint8_t >( EventOp::Add ), 0 } );
		writer.flush();
	}

	const auto events = readBucket( bucketPath( m_dir, bucketFor( HASH_ID ) ) );
	EXPECT_EQ( events.size(), TAG_COUNT );

	std::uint64_t total_elsewhere { 0 };
	for ( std::size_t bucket = 0; bucket < BUCKET_COUNT; ++bucket )
	{
		if ( bucket == bucketFor( HASH_ID ) ) continue;
		total_elsewhere += readBucket( bucketPath( m_dir, bucket ) ).size();
	}
	EXPECT_EQ( total_elsewhere, 0u );
}

TEST_F( BucketSpillTest, SpillsCorrectlyAcrossBufferBoundaries )
{
	// A tiny buffer forces many mid-stream flushes; the readback must still be exact and ordered.
	constexpr std::uint32_t COUNT { 1000 };
	{
		BucketWriter writer { m_dir, 4 };
		for ( std::uint32_t i = 0; i < COUNT; ++i )
			writer.write( MappingEvent { 0, i, 1, static_cast< std::uint8_t >( EventOp::Add ), 0 } );
		writer.flush();
		EXPECT_EQ( writer.written(), COUNT );
	}

	const auto events = readBucket( bucketPath( m_dir, 0 ) );
	ASSERT_EQ( events.size(), COUNT );
	for ( std::uint32_t i = 0; i < COUNT; ++i ) EXPECT_EQ( events[ i ].tag_id, i );
}

TEST_F( BucketSpillTest, MissingBucketReadsAsEmpty )
{
	EXPECT_TRUE( readBucket( m_dir / "does-not-exist.bucket" ).empty() );
}

TEST_F( BucketSpillTest, DestructorFlushesUnwrittenEvents )
{
	{
		BucketWriter writer { m_dir };
		writer.write( MappingEvent { 1, 1, 1, static_cast< std::uint8_t >( EventOp::Delete ), 0 } );
	}

	EXPECT_EQ( readBucket( bucketPath( m_dir, bucketFor( 1 ) ) ).size(), 1u );
}

} // namespace
