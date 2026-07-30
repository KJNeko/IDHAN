//
// Created by kj16609 on 7/30/26.
//

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "SyntheticCorpus.hpp"
#include "ptr/flatten/BucketSpill.hpp"
#include "ptr/flatten/DefinitionStore.hpp"
#include "ptr/flatten/FlattenScan.hpp"

namespace
{

using namespace idhan::hydrus::ptr;
using namespace idhan::hydrus::ptr::test;

class FlattenScanTest : public ::testing::Test
{
  protected:

	void SetUp() override
	{
		m_root = std::filesystem::temp_directory_path() / "ptr-scan-test";
		std::filesystem::remove_all( m_root );
		m_corpus = m_root / "corpus";
		m_work = m_root / "work";
		std::filesystem::create_directories( m_corpus );
		std::filesystem::create_directories( m_work );
	}

	void TearDown() override { std::filesystem::remove_all( m_root ); }

	//! Every event the scan spilled, gathered across all buckets and sorted.
	std::vector< MappingEvent > allEvents() const
	{
		std::vector< MappingEvent > out;
		for ( std::size_t bucket = 0; bucket < BUCKET_COUNT; ++bucket )
		{
			const auto events = readBucket( bucketPath( m_work, bucket ) );
			out.insert( out.end(), events.begin(), events.end() );
		}
		std::ranges::sort( out, eventLess );
		return out;
	}

	std::filesystem::path m_root;
	std::filesystem::path m_corpus;
	std::filesystem::path m_work;
	ScanCallbacks m_callbacks {};
};

TEST_F( FlattenScanTest, WritesDefinitionsAndMappingEvents )
{
	const auto defs_hash = fakeHashHex( 1 );
	const auto content_hash = fakeHashHex( 2 );

	writeUpdateFile( m_corpus, defs_hash, makeDefinitions( { { 100, fakeHashHex( 50 ) } }, { { 7, "solo" } } ) );
	writeUpdateFile( m_corpus, content_hash, makeContent( { { 7, { 100 } } }, {} ) );

	const auto metadata = makeMetadata( { { 0, { defs_hash, content_hash } } } );
	const auto result = scanCorpus( m_corpus, metadata, m_work, m_callbacks );

	EXPECT_FALSE( result.cancelled );
	EXPECT_EQ( result.skipped_files, 0u );
	EXPECT_EQ( result.events_written, 1u );

	const auto events = allEvents();
	ASSERT_EQ( events.size(), 1u );
	EXPECT_EQ( events[ 0 ].hash_id, 100u );
	EXPECT_EQ( events[ 0 ].tag_id, 7u );
	EXPECT_EQ( events[ 0 ].update_index, 0u );
	EXPECT_EQ( events[ 0 ].op, static_cast< std::uint8_t >( EventOp::Add ) );

	const DefinitionReader reader { m_work };
	ASSERT_TRUE( reader.hash( 100 ).has_value() );
	const auto tag = reader.tag( 7 );
	ASSERT_TRUE( tag.has_value() );
	EXPECT_EQ( *tag, "solo" );
}

TEST_F( FlattenScanTest, RecordsAddsAndDeletesWithTheirUpdateIndex )
{
	const auto defs = fakeHashHex( 1 );
	const auto add_file = fakeHashHex( 2 );
	const auto del_file = fakeHashHex( 3 );

	writeUpdateFile( m_corpus, defs, makeDefinitions( { { 100, fakeHashHex( 50 ) } }, { { 7, "solo" } } ) );
	writeUpdateFile( m_corpus, add_file, makeContent( { { 7, { 100 } } }, {} ) );
	writeUpdateFile( m_corpus, del_file, makeContent( {}, { { 7, { 100 } } } ) );

	const auto metadata = makeMetadata( { { 0, { defs, add_file } }, { 5, { del_file } } } );
	const auto result = scanCorpus( m_corpus, metadata, m_work, m_callbacks );

	EXPECT_EQ( result.events_written, 2u );
	EXPECT_EQ( result.first_update_index, 0 );
	EXPECT_EQ( result.last_update_index, 5 );

	const auto events = allEvents();
	ASSERT_EQ( events.size(), 2u );
	EXPECT_EQ( events[ 0 ].op, static_cast< std::uint8_t >( EventOp::Add ) );
	EXPECT_EQ( events[ 0 ].update_index, 0u );
	EXPECT_EQ( events[ 1 ].op, static_cast< std::uint8_t >( EventOp::Delete ) );
	EXPECT_EQ( events[ 1 ].update_index, 5u );
}

TEST_F( FlattenScanTest, ProcessesUpdatesInAscendingIndexOrderRegardlessOfMetadataOrder )
{
	const auto defs = fakeHashHex( 1 );
	const auto later = fakeHashHex( 2 );
	const auto earlier = fakeHashHex( 3 );

	writeUpdateFile( m_corpus, defs, makeDefinitions( { { 1, fakeHashHex( 50 ) } }, { { 1, "a" } } ) );
	writeUpdateFile( m_corpus, later, makeContent( {}, { { 1, { 1 } } } ) );
	writeUpdateFile( m_corpus, earlier, makeContent( { { 1, { 1 } } }, {} ) );

	// Metadata deliberately lists index 9 before index 2.
	const auto metadata = makeMetadata( { { 9, { later } }, { 2, { defs, earlier } } } );
	const auto result = scanCorpus( m_corpus, metadata, m_work, m_callbacks );

	EXPECT_EQ( result.first_update_index, 2 );
	EXPECT_EQ( result.last_update_index, 9 );

	const auto events = allEvents();
	ASSERT_EQ( events.size(), 2u );
	EXPECT_EQ( events[ 0 ].update_index, 2u );
	EXPECT_EQ( events[ 1 ].update_index, 9u );
}

TEST_F( FlattenScanTest, MissingFileIsSkippedAndCounted )
{
	const auto defs = fakeHashHex( 1 );
	writeUpdateFile( m_corpus, defs, makeDefinitions( { { 1, fakeHashHex( 50 ) } }, { { 1, "a" } } ) );

	const auto metadata = makeMetadata( { { 0, { defs, fakeHashHex( 99 ) } } } );
	const auto result = scanCorpus( m_corpus, metadata, m_work, m_callbacks );

	EXPECT_EQ( result.skipped_files, 1u );
	EXPECT_EQ( result.events_written, 0u );
}

TEST_F( FlattenScanTest, UnparseableFileIsSkippedAndCounted )
{
	const auto defs = fakeHashHex( 1 );
	const auto broken = fakeHashHex( 2 );

	writeUpdateFile( m_corpus, defs, makeDefinitions( { { 1, fakeHashHex( 50 ) } }, { { 1, "a" } } ) );
	{
		std::ofstream file { m_corpus / ( broken + ".ptrupdate" ), std::ios::binary };
		file << "this is not zlib";
	}

	const auto metadata = makeMetadata( { { 0, { defs, broken } } } );
	const auto result = scanCorpus( m_corpus, metadata, m_work, m_callbacks );

	// One bad file must not abort the run -- the rest of the corpus still scans.
	EXPECT_EQ( result.skipped_files, 1u );
}

TEST_F( FlattenScanTest, MalformedHashDefinitionIsRejected )
{
	const auto defs = fakeHashHex( 1 );
	writeUpdateFile( m_corpus, defs, makeDefinitions( { { 1, "too-short" } }, { { 1, "a" } } ) );

	const auto metadata = makeMetadata( { { 0, { defs } } } );
	const auto result = scanCorpus( m_corpus, metadata, m_work, m_callbacks );

	EXPECT_EQ( result.rejected_hashes, 1u );

	const DefinitionReader reader { m_work };
	EXPECT_FALSE( reader.hash( 1 ).has_value() );
}

TEST_F( FlattenScanTest, CollectsRelationshipEventsInMemory )
{
	const auto defs = fakeHashHex( 1 );
	const auto content = fakeHashHex( 2 );

	writeUpdateFile( m_corpus, defs, makeDefinitions( {}, { { 1, "bad" }, { 2, "good" }, { 3, "parent" } } ) );
	writeUpdateFile( m_corpus, content, makeContent( {}, {}, { { 1, 2 } }, { { 1, 3 } } ) );

	const auto metadata = makeMetadata( { { 4, { defs, content } } } );
	const auto result = scanCorpus( m_corpus, metadata, m_work, m_callbacks );

	ASSERT_EQ( result.siblings.size(), 1u );
	EXPECT_EQ( result.siblings[ 0 ].a, 1u );
	EXPECT_EQ( result.siblings[ 0 ].b, 2u );
	EXPECT_EQ( result.siblings[ 0 ].update_index, 4u );

	ASSERT_EQ( result.parents.size(), 1u );
	EXPECT_EQ( result.parents[ 0 ].a, 1u );
	EXPECT_EQ( result.parents[ 0 ].b, 3u );
}

TEST_F( FlattenScanTest, StopsWhenCancelled )
{
	const auto defs = fakeHashHex( 1 );
	writeUpdateFile( m_corpus, defs, makeDefinitions( { { 1, fakeHashHex( 50 ) } }, { { 1, "a" } } ) );

	for ( unsigned i = 2; i < 12; ++i )
		writeUpdateFile( m_corpus, fakeHashHex( i ), makeContent( { { 1, { 1 } } }, {} ) );

	std::vector< std::string > hashes { defs };
	for ( unsigned i = 2; i < 12; ++i ) hashes.push_back( fakeHashHex( i ) );

	int seen { 0 };
	m_callbacks.cancelled = [ &seen ] { return seen++ >= 3; };

	const auto result = scanCorpus( m_corpus, makeMetadata( { { 0, hashes } } ), m_work, m_callbacks );

	EXPECT_TRUE( result.cancelled );
	EXPECT_LT( result.events_written, 10u );
}

TEST_F( FlattenScanTest, RejectsAnUpdateIndexBeyondTheSixteenBitRange )
{
	const auto defs = fakeHashHex( 1 );
	writeUpdateFile( m_corpus, defs, makeDefinitions( { { 1, fakeHashHex( 50 ) } }, { { 1, "a" } } ) );

	const auto metadata = makeMetadata( { { static_cast< int >( MAX_UPDATE_INDEX ) + 1, { defs } } } );

	// Silently truncating would corrupt every chain's ordering, so this must be loud.
	EXPECT_THROW( ( void ) scanCorpus( m_corpus, metadata, m_work, m_callbacks ), std::runtime_error );
}

} // namespace
