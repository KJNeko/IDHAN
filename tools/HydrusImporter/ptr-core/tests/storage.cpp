#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <format>
#include <functional>
#include <random>
#include <thread>

#include "ptr/flatten/BucketSpill.hpp"
#include "ptr/flatten/DefinitionStore.hpp"

namespace idhan::hydrus::ptr::test
{

struct StorageDirectory
{
	std::filesystem::path path {
		std::filesystem::temp_directory_path() / std::format( "idhan-ptr-{}", std::random_device {}() )
		/ std::filesystem::path( u8"定義" )
	};

	StorageDirectory() { std::filesystem::create_directories( path ); }

	~StorageDirectory()
	{
		std::error_code error;
		std::filesystem::remove_all( path.parent_path(), error );
	}
};

TEST_CASE( "PTR definitions preserve sparse offsets beyond 4 GiB", "[ptr][storage]" )
{
	StorageDirectory directory;
	constexpr std::uint32_t high_id { ( 1U << 27 ) + 7 };
	const std::string hash( 64, 'a' );
	{
		DefinitionWriter writer { directory.path };
		REQUIRE( writer.writeHash( high_id, hash ) );
		writer.writeTag( 5, "series:example" );
	}

	REQUIRE(
		std::filesystem::file_size( directory.path / HASHES_FILENAME )
	    == ( static_cast< std::uint64_t >( high_id ) + 1 ) * SHA256_BYTES );
	DefinitionReader reader { directory.path };
	const auto value = reader.hash( high_id );
	REQUIRE( value.has_value() );
	REQUIRE( std::ranges::all_of( value.value(), []( const std::byte byte ) { return byte == std::byte { 0xaa }; } ) );
	REQUIRE_FALSE( reader.hash( high_id - 1 ).has_value() );
	REQUIRE_FALSE( reader.hash( high_id + 1 ).has_value() );
	REQUIRE( reader.tag( 5 ) == "series:example" );
	REQUIRE_FALSE( reader.tag( 4 ).has_value() );
	REQUIRE( reader.definedTagCount() == 1 );
}

inline constexpr std::uint32_t DEFINITIONS_PER_THREAD { 48 };

void writeDefinitions( DefinitionWriter& writer, const std::uint32_t thread )
{
	for ( std::uint32_t index {}; index < DEFINITIONS_PER_THREAD; ++index )
	{
		const auto id = thread * DEFINITIONS_PER_THREAD + index;
		writer.writeHash( id, std::format( "{:064x}", id + 1 ) );
		writer.writeTag( id, std::format( "tag:{}", id ) );
	}
}

TEST_CASE( "Concurrent PTR definition writes keep independent offsets", "[ptr][storage]" )
{
	StorageDirectory directory;
	constexpr std::uint32_t threads { 8 };
	{
		DefinitionWriter writer { directory.path };
		std::vector< std::jthread > workers;
		for ( std::uint32_t thread {}; thread < threads; ++thread )
			workers.emplace_back( writeDefinitions, std::ref( writer ), thread );
	}

	DefinitionReader reader { directory.path };
	REQUIRE( reader.definedTagCount() == threads * DEFINITIONS_PER_THREAD );
	for ( std::uint32_t id {}; id < threads * DEFINITIONS_PER_THREAD; ++id )
	{
		INFO( "definition id: " << id );
		const auto hash = reader.hash( id );
		REQUIRE( hash.has_value() );
		const auto expected = decodeSha256Hex( std::format( "{:064x}", id + 1 ) );
		REQUIRE( expected.has_value() );
		REQUIRE( std::ranges::equal( hash.value(), expected.value() ) );
		REQUIRE( reader.tag( id ) == std::format( "tag:{}", id ) );
	}
}

TEST_CASE( "PTR buckets round trip binary records in Unicode directories", "[ptr][storage]" )
{
	StorageDirectory directory;
	constexpr MappingEvent event { 10, 26, 13, static_cast< std::uint8_t >( EventOp::Add ), 0 };
	{
		BucketWriter writer { directory.path, 1 };
		writer.write( event );
		writer.write( event );
	}

	const auto events = readBucket( bucketPath( directory.path, bucketFor( event.hash_id ) ) );
	REQUIRE( events.size() == 2 );
	for ( const auto& stored : events )
	{
		REQUIRE( stored.hash_id == event.hash_id );
		REQUIRE( stored.tag_id == event.tag_id );
		REQUIRE( stored.update_index == event.update_index );
		REQUIRE( stored.op == event.op );
	}
}

} // namespace idhan::hydrus::ptr::test
