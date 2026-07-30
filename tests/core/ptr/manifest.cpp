//
// Created by kj16609 on 7/30/26.
//

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "ptr/flatten/Manifest.hpp"

namespace
{

using namespace idhan::hydrus::ptr;

class ManifestTest : public ::testing::Test
{
  protected:

	void SetUp() override
	{
		m_dir = std::filesystem::temp_directory_path() / "ptr-manifest-test";
		std::filesystem::remove_all( m_dir );
		std::filesystem::create_directories( m_dir );
	}

	void TearDown() override { std::filesystem::remove_all( m_dir ); }

	static CompactManifest sample()
	{
		CompactManifest manifest {};
		manifest.first_update_index = 0;
		manifest.last_update_index = 2599;
		manifest.max_records_per_chunk = 200'000;
		manifest.relations_file = "relations.idhanptr";
		manifest.chunks.push_back( ChunkEntry { "chunk-00000.idhanptr", 200'000, 3'800'000 } );
		manifest.chunks.push_back( ChunkEntry { "chunk-00001.idhanptr", 12'345, 210'000 } );
		manifest.stats.events_scanned = 3'200'000'000;
		manifest.stats.mappings_after_collapse = 2'900'000'000;
		manifest.stats.events_collapsed = 300'000'000;
		manifest.stats.terminal_deletes = 1'500'000;
		manifest.stats.skipped_files = 3;
		manifest.stats.skipped_missing_definitions = 91;
		return manifest;
	}

	std::filesystem::path m_dir;
};

TEST_F( ManifestTest, RoundTrips )
{
	writeManifest( m_dir, sample() );
	const auto read = readManifest( m_dir );

	EXPECT_EQ( read.format_version, CHUNK_FORMAT_VERSION );
	EXPECT_EQ( read.first_update_index, 0 );
	EXPECT_EQ( read.last_update_index, 2599 );
	EXPECT_EQ( read.max_records_per_chunk, 200'000u );
	EXPECT_EQ( read.relations_file, "relations.idhanptr" );

	ASSERT_EQ( read.chunks.size(), 2u );
	EXPECT_EQ( read.chunks[ 0 ].file, "chunk-00000.idhanptr" );
	EXPECT_EQ( read.chunks[ 0 ].records, 200'000u );
	EXPECT_EQ( read.chunks[ 0 ].mappings, 3'800'000u );
	EXPECT_EQ( read.chunks[ 1 ].records, 12'345u );

	// The counters exceed 32 bits; they must survive as 64-bit values.
	EXPECT_EQ( read.stats.events_scanned, 3'200'000'000u );
	EXPECT_EQ( read.stats.mappings_after_collapse, 2'900'000'000u );
	EXPECT_EQ( read.stats.events_collapsed, 300'000'000u );
	EXPECT_EQ( read.stats.terminal_deletes, 1'500'000u );
	EXPECT_EQ( read.stats.skipped_files, 3u );
	EXPECT_EQ( read.stats.skipped_missing_definitions, 91u );
}

TEST_F( ManifestTest, DirectoryWithoutAManifestIsNotCompacted )
{
	EXPECT_FALSE( isCompactedDirectory( m_dir ) );
}

TEST_F( ManifestTest, DirectoryWithAManifestIsCompacted )
{
	writeManifest( m_dir, sample() );
	EXPECT_TRUE( isCompactedDirectory( m_dir ) );
}

TEST_F( ManifestTest, UnparseableManifestIsNotCompacted )
{
	{
		std::ofstream file { m_dir / MANIFEST_FILENAME };
		file << "{ this is not json";
	}
	EXPECT_FALSE( isCompactedDirectory( m_dir ) );
}

TEST_F( ManifestTest, ManifestFromAFutureVersionIsNotCompacted )
{
	auto manifest = sample();
	manifest.format_version = CHUNK_FORMAT_VERSION + 1;
	writeManifest( m_dir, manifest );

	// Refusing an unknown version is what stops a newer flattener's output being misread.
	EXPECT_FALSE( isCompactedDirectory( m_dir ) );
}

TEST_F( ManifestTest, ReadingAMissingManifestThrows )
{
	EXPECT_THROW( ( void ) readManifest( m_dir ), std::runtime_error );
}

TEST_F( ManifestTest, RoundTripsWithNoChunks )
{
	CompactManifest manifest {};
	manifest.relations_file = "relations.idhanptr";
	writeManifest( m_dir, manifest );

	const auto read = readManifest( m_dir );
	EXPECT_TRUE( read.chunks.empty() );
	EXPECT_TRUE( isCompactedDirectory( m_dir ) );
}

} // namespace
