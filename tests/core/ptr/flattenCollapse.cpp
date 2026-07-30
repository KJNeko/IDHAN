//
// Created by kj16609 on 7/30/26.
//

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "SyntheticCorpus.hpp"
#include "ptr/flatten/BucketSpill.hpp"
#include "ptr/flatten/ChunkFormat.hpp"
#include "ptr/flatten/DefinitionStore.hpp"
#include "ptr/flatten/FlattenCollapse.hpp"

namespace
{

using namespace idhan::hydrus::ptr;
using namespace idhan::hydrus::ptr::test;

class FlattenCollapseTest : public ::testing::Test
{
  protected:

	void SetUp() override
	{
		m_root = std::filesystem::temp_directory_path() / "ptr-collapse-test";
		std::filesystem::remove_all( m_root );
		m_work = m_root / "work";
		m_out = m_root / "out";
		std::filesystem::create_directories( m_work );
		std::filesystem::create_directories( m_out );
	}

	void TearDown() override { std::filesystem::remove_all( m_root ); }

	//! Writes the definitions the collapse stage will resolve against.
	//!
	//! Hashes and tags go through ONE writer: DefinitionWriter truncates on construction, so two
	//! successive writers would leave only whatever the second one wrote. Hash definitions are
	//! derived from the id so their first byte equals it, which is what lets readAll key on it.
	void define( const std::vector< std::uint32_t >& hash_ids,
	             const std::vector< std::pair< std::uint32_t, std::string > >& tags ) const
	{
		DefinitionWriter writer { m_work };
		for ( const auto id : hash_ids ) writer.writeHash( id, fakeHashHex( id ) );
		for ( const auto& [ id, text ] : tags ) writer.writeTag( id, text );
	}

	void spill( const std::vector< MappingEvent >& events ) const
	{
		BucketWriter writer { m_work };
		for ( const auto& event : events ) writer.write( event );
	}

	struct FlatRecord
	{
		std::vector< std::string > adds {};
		std::vector< std::string > dels {};
	};

	//! Every record across every emitted chunk, keyed by its first hash byte, tags resolved.
	std::map< unsigned, FlatRecord > readAll( const CollapseResult& result ) const
	{
		std::map< unsigned, FlatRecord > out;
		for ( const auto& entry : result.chunks )
		{
			const auto chunk = readChunk( m_out / entry.file );
			for ( const auto& record : chunk.records )
			{
				FlatRecord flat {};
				for ( const auto index : record.add_indices ) flat.adds.push_back( chunk.strings[ index ].tag );
				for ( const auto index : record.del_indices ) flat.dels.push_back( chunk.strings[ index ].tag );
				std::ranges::sort( flat.adds );
				std::ranges::sort( flat.dels );
				out.emplace( std::to_integer< unsigned >( record.sha256[ 0 ] ), std::move( flat ) );
			}
		}
		return out;
	}

	static MappingEvent
		ev( const std::uint32_t hash_id, const std::uint32_t tag_id, const std::uint16_t index, const EventOp op )
	{
		return MappingEvent { hash_id, tag_id, index, static_cast< std::uint8_t >( op ), 0 };
	}

	std::filesystem::path m_root;
	std::filesystem::path m_work;
	std::filesystem::path m_out;
	CollapseCallbacks m_callbacks {};
};

TEST_F( FlattenCollapseTest, EmitsASurvivingAdd )
{
	define( { 1 }, { { 7, "solo" } } );
	spill( { ev( 1, 7, 0, EventOp::Add ) } );

	const DefinitionReader definitions { m_work };
	const auto result = collapseBuckets( m_work, m_out, definitions, MAX_RECORDS_PER_CHUNK, m_callbacks );

	EXPECT_FALSE( result.cancelled );
	EXPECT_EQ( result.stats.mappings_after_collapse, 1u );

	const auto records = readAll( result );
	ASSERT_EQ( records.size(), 1u );
	EXPECT_EQ( records.begin()->second.adds, std::vector< std::string > { "solo" } );
	EXPECT_TRUE( records.begin()->second.dels.empty() );
}

TEST_F( FlattenCollapseTest, CollapsesAddDeleteAddToASingleAdd )
{
	define( { 1 }, { { 7, "solo" } } );
	spill( { ev( 1, 7, 0, EventOp::Add ), ev( 1, 7, 1, EventOp::Delete ), ev( 1, 7, 2, EventOp::Add ) } );

	const DefinitionReader definitions { m_work };
	const auto result = collapseBuckets( m_work, m_out, definitions, MAX_RECORDS_PER_CHUNK, m_callbacks );

	EXPECT_EQ( result.stats.events_scanned, 3u );
	EXPECT_EQ( result.stats.mappings_after_collapse, 1u );
	EXPECT_EQ( result.stats.events_collapsed, 2u );

	const auto records = readAll( result );
	ASSERT_EQ( records.size(), 1u );
	EXPECT_EQ( records.begin()->second.adds, std::vector< std::string > { "solo" } );
	EXPECT_TRUE( records.begin()->second.dels.empty() );
}

TEST_F( FlattenCollapseTest, CollapsesAddDeleteAddDeleteToASingleDelete )
{
	define( { 1 }, { { 7, "solo" } } );
	spill( { ev( 1, 7, 0, EventOp::Add ),
	         ev( 1, 7, 1, EventOp::Delete ),
	         ev( 1, 7, 2, EventOp::Add ),
	         ev( 1, 7, 3, EventOp::Delete ) } );

	const DefinitionReader definitions { m_work };
	const auto result = collapseBuckets( m_work, m_out, definitions, MAX_RECORDS_PER_CHUNK, m_callbacks );

	EXPECT_EQ( result.stats.terminal_deletes, 1u );

	const auto records = readAll( result );
	ASSERT_EQ( records.size(), 1u );
	EXPECT_TRUE( records.begin()->second.adds.empty() );
	EXPECT_EQ( records.begin()->second.dels, std::vector< std::string > { "solo" } );
}

TEST_F( FlattenCollapseTest, GroupsEveryTagForOneRecordIntoOneEntry )
{
	// The whole point of record-major output: a record touched across many updates appears once.
	define( { 1 }, { { 1, "a" }, { 2, "b" }, { 3, "c" } } );
	spill( { ev( 1, 1, 0, EventOp::Add ), ev( 1, 2, 5, EventOp::Add ), ev( 1, 3, 9, EventOp::Add ) } );

	const DefinitionReader definitions { m_work };
	const auto result = collapseBuckets( m_work, m_out, definitions, MAX_RECORDS_PER_CHUNK, m_callbacks );

	const auto records = readAll( result );
	ASSERT_EQ( records.size(), 1u );
	EXPECT_EQ( records.begin()->second.adds, ( std::vector< std::string > { "a", "b", "c" } ) );
}

TEST_F( FlattenCollapseTest, SeparatesAddsAndDeletesOnOneRecord )
{
	define( { 1 }, { { 1, "kept" }, { 2, "removed" } } );
	spill( { ev( 1, 1, 0, EventOp::Add ), ev( 1, 2, 0, EventOp::Add ), ev( 1, 2, 3, EventOp::Delete ) } );

	const DefinitionReader definitions { m_work };
	const auto result = collapseBuckets( m_work, m_out, definitions, MAX_RECORDS_PER_CHUNK, m_callbacks );

	const auto records = readAll( result );
	ASSERT_EQ( records.size(), 1u );
	EXPECT_EQ( records.begin()->second.adds, std::vector< std::string > { "kept" } );
	EXPECT_EQ( records.begin()->second.dels, std::vector< std::string > { "removed" } );
}

TEST_F( FlattenCollapseTest, ChunkCapSplitsOutputAndSpansBuckets )
{
	// 40 records spread over many buckets with a cap of 8 must produce several chunks. If a chunk
	// were confined to a single bucket the cap would never be reached and this would emit one
	// chunk per occupied bucket instead.
	constexpr std::uint32_t RECORDS { 40 };

	std::vector< std::uint32_t > hash_ids;
	std::vector< MappingEvent > events;
	for ( std::uint32_t i = 1; i <= RECORDS; ++i )
	{
		hash_ids.push_back( i );
		events.push_back( ev( i, 1, 0, EventOp::Add ) );
	}

	define( hash_ids, { { 1, "a" } } );
	spill( events );

	const DefinitionReader definitions { m_work };
	const auto result = collapseBuckets( m_work, m_out, definitions, 8, m_callbacks );

	std::uint64_t total { 0 };
	for ( const auto& entry : result.chunks )
	{
		EXPECT_LE( entry.records, 8u );
		total += entry.records;
	}
	EXPECT_EQ( total, RECORDS );
	EXPECT_GE( result.chunks.size(), 5u );
	EXPECT_LE( result.chunks.size(), 6u );
}

TEST_F( FlattenCollapseTest, TagWithNoDefinitionIsDroppedAndCounted )
{
	define( { 1 }, { { 1, "known" } } );
	spill( { ev( 1, 1, 0, EventOp::Add ), ev( 1, 999, 0, EventOp::Add ) } );

	const DefinitionReader definitions { m_work };
	const auto result = collapseBuckets( m_work, m_out, definitions, MAX_RECORDS_PER_CHUNK, m_callbacks );

	EXPECT_EQ( result.stats.skipped_missing_definitions, 1u );

	const auto records = readAll( result );
	ASSERT_EQ( records.size(), 1u );
	EXPECT_EQ( records.begin()->second.adds, std::vector< std::string > { "known" } );
}

TEST_F( FlattenCollapseTest, RecordWithNoHashDefinitionIsDropped )
{
	// Without a hash there is nothing to address the record by, so it cannot be emitted.
	define( {}, { { 1, "a" } } );
	spill( { ev( 1, 1, 0, EventOp::Add ) } );

	const DefinitionReader definitions { m_work };
	const auto result = collapseBuckets( m_work, m_out, definitions, MAX_RECORDS_PER_CHUNK, m_callbacks );

	EXPECT_TRUE( readAll( result ).empty() );
}

TEST_F( FlattenCollapseTest, StatsUpdatedReportsRunningTotals )
{
	// Two records on different hashes land in different buckets (hash_id % BUCKET_COUNT), so the
	// callback should fire at least twice and its last call should match the final result.
	define( { 1, 2 }, { { 7, "kept" }, { 8, "removed" } } );
	spill( { ev( 1, 7, 0, EventOp::Add ),
	         ev( 2, 8, 0, EventOp::Add ),
	         ev( 2, 8, 1, EventOp::Delete ),
	         ev( 2, 8, 2, EventOp::Add ),
	         ev( 2, 8, 3, EventOp::Delete ) } );

	struct Snapshot
	{
		std::uint64_t records_flattened;
		std::uint64_t chains_collapsed;
		std::uint64_t terminal_deletes;
		std::uint64_t chunks_written;
		std::uint64_t skipped_missing_definitions;
	};
	std::vector< Snapshot > seen;
	m_callbacks.statsUpdated = [ &seen ]( const std::uint64_t records_flattened,
	                                      const std::uint64_t chains_collapsed,
	                                      const std::uint64_t terminal_deletes,
	                                      const std::uint64_t chunks_written,
	                                      const std::uint64_t skipped_missing_definitions )
	{ seen.push_back( { records_flattened, chains_collapsed, terminal_deletes, chunks_written,
		                skipped_missing_definitions } ); };

	const DefinitionReader definitions { m_work };
	const auto result = collapseBuckets( m_work, m_out, definitions, MAX_RECORDS_PER_CHUNK, m_callbacks );

	ASSERT_FALSE( seen.empty() );
	// Two occupied buckets, plus one final call once the trailing open chunk is closed.
	EXPECT_EQ( seen.size(), 3u );

	const auto& last = seen.back();
	EXPECT_EQ( last.records_flattened, 2u );
	EXPECT_EQ( last.chains_collapsed, result.stats.events_collapsed );
	EXPECT_EQ( last.terminal_deletes, result.stats.terminal_deletes );
	EXPECT_EQ( last.skipped_missing_definitions, 0u );

	// Running totals never decrease across calls.
	for ( std::size_t i = 1; i < seen.size(); ++i )
	{
		EXPECT_GE( seen[ i ].records_flattened, seen[ i - 1 ].records_flattened );
		EXPECT_GE( seen[ i ].chunks_written, seen[ i - 1 ].chunks_written );
	}
}

TEST_F( FlattenCollapseTest, EmptyWorkDirectoryProducesNoChunks )
{
	const DefinitionReader definitions { m_work };
	const auto result = collapseBuckets( m_work, m_out, definitions, MAX_RECORDS_PER_CHUNK, m_callbacks );

	EXPECT_TRUE( result.chunks.empty() );
	EXPECT_EQ( result.stats.events_scanned, 0u );
}

TEST_F( FlattenCollapseTest, StopsWhenCancelled )
{
	std::vector< std::uint32_t > hash_ids;
	std::vector< MappingEvent > events;
	for ( std::uint32_t i = 1; i <= 200; ++i )
	{
		hash_ids.push_back( i );
		events.push_back( ev( i, 1, 0, EventOp::Add ) );
	}
	define( hash_ids, { { 1, "a" } } );
	spill( events );

	m_callbacks.cancelled = [] { return true; };

	const DefinitionReader definitions { m_work };
	const auto result = collapseBuckets( m_work, m_out, definitions, 8, m_callbacks );

	EXPECT_TRUE( result.cancelled );
}

} // namespace
