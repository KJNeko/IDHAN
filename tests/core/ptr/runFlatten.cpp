//
// Created by kj16609 on 7/30/26.
//

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "SyntheticCorpus.hpp"
#include "ptr/flatten/ChunkFormat.hpp"
#include "ptr/flatten/Manifest.hpp"
#include "ptr/flatten/RelationsFile.hpp"
#include "ptr/flatten/RunFlatten.hpp"

namespace
{

using namespace idhan::hydrus::ptr;
using namespace idhan::hydrus::ptr::test;

class RunFlattenTest : public ::testing::Test
{
  protected:

	void SetUp() override
	{
		m_root = std::filesystem::temp_directory_path() / "ptr-runflatten-test";
		std::filesystem::remove_all( m_root );
		m_corpus = m_root / "corpus";
		m_out = m_root / "out";
		std::filesystem::create_directories( m_corpus );
	}

	void TearDown() override { std::filesystem::remove_all( m_root ); }

	//! Writes a ptr_metadata.json in the shape parseMetadataCacheJson expects.
	void writeMetadataCache( const std::vector< std::pair< int, std::vector< std::string > > >& updates ) const
	{
		Json::Value root { Json::objectValue };
		Json::Value list { Json::arrayValue };
		for ( const auto& [ index, hashes ] : updates )
		{
			Json::Value entry { Json::objectValue };
			entry[ "index" ] = index;
			Json::Value hash_array { Json::arrayValue };
			for ( const auto& hash : hashes ) hash_array.append( hash );
			entry[ "hashes" ] = hash_array;
			entry[ "begin" ] = 0;
			entry[ "end" ] = 0;
			list.append( entry );
		}
		root[ "updates" ] = list;
		root[ "state" ] = "done";

		std::ofstream file { m_corpus / "ptr_metadata.json", std::ios::trunc };
		Json::StreamWriterBuilder builder;
		file << Json::writeString( builder, root );
	}

	//! Tag text applied to each record in the output, keyed by record hash hex.
	std::map< std::string, std::vector< std::string > > readAdds() const
	{
		std::map< std::string, std::vector< std::string > > out;
		const auto manifest = readManifest( m_out );

		for ( const auto& entry : manifest.chunks )
		{
			const auto chunk = readChunk( m_out / entry.file );
			for ( const auto& record : chunk.records )
			{
				std::string hex;
				for ( const auto byte : record.sha256 )
				{
					constexpr char DIGITS[] = "0123456789abcdef";
					const auto value = std::to_integer< unsigned >( byte );
					hex.push_back( DIGITS[ ( value >> 4 ) & 0xF ] );
					hex.push_back( DIGITS[ value & 0xF ] );
				}

				std::vector< std::string > tags;
				for ( const auto index : record.add_indices ) tags.push_back( chunk.strings[ index ].tag );
				std::ranges::sort( tags );
				out.emplace( hex, std::move( tags ) );
			}
		}
		return out;
	}

	std::filesystem::path m_root;
	std::filesystem::path m_corpus;
	std::filesystem::path m_out;
	FlattenCallbacks m_callbacks {};
};

TEST_F( RunFlattenTest, FlattensAChainAcrossUpdateFilesIntoOneAdd )
{
	// The motivating case end to end: tag 7 on record 100 is added, deleted, then re-added across
	// three separate update files. The compacted output must show exactly one add.
	const auto defs = fakeHashHex( 1 );
	const auto add1 = fakeHashHex( 2 );
	const auto del = fakeHashHex( 3 );
	const auto add2 = fakeHashHex( 4 );
	const auto record_hex = fakeHashHex( 100 );

	writeUpdateFile( m_corpus, defs, makeDefinitions( { { 100, record_hex } }, { { 7, "solo" } } ) );
	writeUpdateFile( m_corpus, add1, makeContent( { { 7, { 100 } } }, {} ) );
	writeUpdateFile( m_corpus, del, makeContent( {}, { { 7, { 100 } } } ) );
	writeUpdateFile( m_corpus, add2, makeContent( { { 7, { 100 } } }, {} ) );

	writeMetadataCache( { { 0, { defs, add1 } }, { 1, { del } }, { 2, { add2 } } } );

	const auto outcome = runFlatten( m_corpus, m_out, m_callbacks );

	ASSERT_TRUE( outcome.success ) << outcome.message;
	EXPECT_FALSE( outcome.cancelled );

	const auto adds = readAdds();
	ASSERT_EQ( adds.size(), 1u );
	EXPECT_EQ( adds.at( record_hex ), std::vector< std::string > { "solo" } );

	const auto manifest = readManifest( m_out );
	EXPECT_EQ( manifest.stats.events_scanned, 3u );
	EXPECT_EQ( manifest.stats.mappings_after_collapse, 1u );
	EXPECT_EQ( manifest.stats.events_collapsed, 2u );
	EXPECT_EQ( manifest.first_update_index, 0 );
	EXPECT_EQ( manifest.last_update_index, 2 );
	EXPECT_EQ( manifest.max_records_per_chunk, MAX_RECORDS_PER_CHUNK );
}

TEST_F( RunFlattenTest, ProducesACompactedDirectory )
{
	const auto defs = fakeHashHex( 1 );
	const auto content = fakeHashHex( 2 );

	writeUpdateFile( m_corpus, defs, makeDefinitions( { { 1, fakeHashHex( 100 ) } }, { { 1, "a" } } ) );
	writeUpdateFile( m_corpus, content, makeContent( { { 1, { 1 } } }, {} ) );
	writeMetadataCache( { { 0, { defs, content } } } );

	ASSERT_TRUE( runFlatten( m_corpus, m_out, m_callbacks ).success );

	EXPECT_TRUE( isCompactedDirectory( m_out ) );
	EXPECT_TRUE( std::filesystem::exists( m_out / RELATIONS_FILENAME ) );
}

TEST_F( RunFlattenTest, RemovesTheWorkDirectoryOnSuccess )
{
	const auto defs = fakeHashHex( 1 );
	writeUpdateFile( m_corpus, defs, makeDefinitions( { { 1, fakeHashHex( 100 ) } }, { { 1, "a" } } ) );
	writeMetadataCache( { { 0, { defs } } } );

	ASSERT_TRUE( runFlatten( m_corpus, m_out, m_callbacks ).success );

	// The buckets and definition store are scratch; leaving 45 GB behind would be a bug.
	EXPECT_FALSE( std::filesystem::exists( m_out / WORK_SUBDIRECTORY ) );
}

TEST_F( RunFlattenTest, TerminalDeleteSurvivesAsADelete )
{
	const auto defs = fakeHashHex( 1 );
	const auto add = fakeHashHex( 2 );
	const auto del = fakeHashHex( 3 );
	const auto record_hex = fakeHashHex( 100 );

	writeUpdateFile( m_corpus, defs, makeDefinitions( { { 100, record_hex } }, { { 7, "solo" } } ) );
	writeUpdateFile( m_corpus, add, makeContent( { { 7, { 100 } } }, {} ) );
	writeUpdateFile( m_corpus, del, makeContent( {}, { { 7, { 100 } } } ) );
	writeMetadataCache( { { 0, { defs, add } }, { 1, { del } } } );

	ASSERT_TRUE( runFlatten( m_corpus, m_out, m_callbacks ).success );

	const auto manifest = readManifest( m_out );
	EXPECT_EQ( manifest.stats.terminal_deletes, 1u );

	ASSERT_FALSE( manifest.chunks.empty() );
	const auto chunk = readChunk( m_out / manifest.chunks.at( 0 ).file );
	ASSERT_EQ( chunk.records.size(), 1u );
	EXPECT_TRUE( chunk.records[ 0 ].add_indices.empty() );
	ASSERT_EQ( chunk.records[ 0 ].del_indices.size(), 1u );
	EXPECT_EQ( chunk.strings[ chunk.records[ 0 ].del_indices[ 0 ] ].tag, "solo" );
}

TEST_F( RunFlattenTest, WritesCollapsedRelations )
{
	const auto defs = fakeHashHex( 1 );
	const auto content = fakeHashHex( 2 );

	writeUpdateFile( m_corpus, defs, makeDefinitions( {}, { { 1, "bad" }, { 2, "good" }, { 3, "parent" } } ) );
	writeUpdateFile( m_corpus, content, makeContent( {}, {}, { { 1, 2 } }, { { 1, 3 } } ) );
	writeMetadataCache( { { 0, { defs, content } } } );

	ASSERT_TRUE( runFlatten( m_corpus, m_out, m_callbacks ).success );

	const auto relations = readRelationsFile( m_out / RELATIONS_FILENAME );
	ASSERT_EQ( relations.siblings.size(), 1u );
	EXPECT_EQ( relations.strings[ relations.siblings[ 0 ].a_index ].tag, "bad" );
	EXPECT_EQ( relations.strings[ relations.siblings[ 0 ].b_index ].tag, "good" );
	ASSERT_EQ( relations.parents.size(), 1u );
	EXPECT_EQ( relations.strings[ relations.parents[ 0 ].b_index ].tag, "parent" );
}

TEST_F( RunFlattenTest, GroupsManyUpdatesIntoFarFewerChunks )
{
	// The grouping win: 60 update files touching 5 records must compact to a single chunk with
	// each record appearing exactly once.
	const auto defs = fakeHashHex( 1 );

	std::vector< std::pair< std::uint32_t, std::string > > hash_defs;
	std::vector< std::pair< std::uint32_t, std::string > > tag_defs;
	for ( std::uint32_t r = 1; r <= 5; ++r ) hash_defs.emplace_back( r, fakeHashHex( 100 + r ) );
	for ( std::uint32_t t = 1; t <= 60; ++t ) tag_defs.emplace_back( t, "tag:" + std::to_string( t ) );

	writeUpdateFile( m_corpus, defs, makeDefinitions( hash_defs, tag_defs ) );

	std::vector< std::pair< int, std::vector< std::string > > > updates { { 0, { defs } } };
	for ( std::uint32_t t = 1; t <= 60; ++t )
	{
		const auto file = fakeHashHex( 200 + t );
		writeUpdateFile( m_corpus, file, makeContent( { { t, { 1, 2, 3, 4, 5 } } }, {} ) );
		updates.push_back( { static_cast< int >( t ), { file } } );
	}
	writeMetadataCache( updates );

	ASSERT_TRUE( runFlatten( m_corpus, m_out, m_callbacks ).success );

	const auto manifest = readManifest( m_out );
	EXPECT_EQ( manifest.chunks.size(), 1u );
	EXPECT_EQ( manifest.stats.events_scanned, 300u );
	EXPECT_EQ( manifest.stats.mappings_after_collapse, 300u );

	const auto adds = readAdds();
	EXPECT_EQ( adds.size(), 5u );
	for ( const auto& [ hex, tags ] : adds ) EXPECT_EQ( tags.size(), 60u );
}

TEST_F( RunFlattenTest, CancelledRunLeavesNoManifest )
{
	const auto defs = fakeHashHex( 1 );
	writeUpdateFile( m_corpus, defs, makeDefinitions( { { 1, fakeHashHex( 100 ) } }, { { 1, "a" } } ) );
	for ( unsigned i = 2; i < 20; ++i )
		writeUpdateFile( m_corpus, fakeHashHex( i ), makeContent( { { 1, { 1 } } }, {} ) );

	std::vector< std::string > hashes { defs };
	for ( unsigned i = 2; i < 20; ++i ) hashes.push_back( fakeHashHex( i ) );
	writeMetadataCache( { { 0, hashes } } );

	m_callbacks.cancelled = [] { return true; };

	const auto outcome = runFlatten( m_corpus, m_out, m_callbacks );

	EXPECT_TRUE( outcome.cancelled );
	EXPECT_FALSE( outcome.success );

	// This is the safety property: without a manifest the directory is not importable.
	EXPECT_FALSE( isCompactedDirectory( m_out ) );
}

TEST_F( RunFlattenTest, MissingMetadataFails )
{
	const auto outcome = runFlatten( m_corpus, m_out, m_callbacks );

	EXPECT_FALSE( outcome.success );
	EXPECT_FALSE( outcome.message.empty() );
	EXPECT_FALSE( isCompactedDirectory( m_out ) );
}

TEST_F( RunFlattenTest, ReportsProgress )
{
	const auto defs = fakeHashHex( 1 );
	const auto content = fakeHashHex( 2 );
	writeUpdateFile( m_corpus, defs, makeDefinitions( { { 1, fakeHashHex( 100 ) } }, { { 1, "a" } } ) );
	writeUpdateFile( m_corpus, content, makeContent( { { 1, { 1 } } }, {} ) );
	writeMetadataCache( { { 0, { defs, content } } } );

	int stages { 0 };
	m_callbacks.stage = [ &stages ]( const std::string_view ) { ++stages; };

	ASSERT_TRUE( runFlatten( m_corpus, m_out, m_callbacks ).success );
	EXPECT_GE( stages, 3 );
}

} // namespace
