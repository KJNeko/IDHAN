#include <gtest/gtest.h>

#include <fcntl.h>
#include <numeric>
#include <unistd.h>
#include <vector>

#include "ipc/Blob.hpp"
#include "ipc/MemfdSink.hpp"

using namespace idhan;

[[nodiscard]] std::vector< std::byte > pattern( const std::size_t size, const std::uint8_t salt = 0 )
{
	std::vector< std::byte > out( size );
	for ( std::size_t i = 0; i < size; ++i )
		out[ i ] = static_cast< std::byte >( ( ( i * 17u ) + salt ) & 0xFFu );
	return out;
}

//! Maps what a finished sink produced, the way the host does when the RESULT frame arrives.
[[nodiscard]] std::vector< std::byte > readBack( ipc::UniqueFd fd )
{
	auto blob { ipc::Blob::adopt( std::move( fd ) ) };
	EXPECT_TRUE( blob.has_value() );
	if ( !blob ) return {};

	const auto bytes { blob->bytes() };
	return std::vector< std::byte > { bytes.begin(), bytes.end() };
}


//! A reserved sink round-trips exactly what was written.
TEST( MemfdSink, ReservedRoundTrip )
{
	const auto expected { pattern( 64 * 1024 ) };

	auto sink { ipc::MemfdSink::create() };
	ASSERT_TRUE( sink.has_value() );

	ASSERT_TRUE( ( *sink )->reserve( expected.size() ).has_value() );
	ASSERT_TRUE( ( *sink )->write( expected ).has_value() );
	EXPECT_EQ( ( *sink )->written(), expected.size() );

	auto fd { ( *sink )->seal() };
	ASSERT_TRUE( fd.has_value() );

	EXPECT_EQ( readBack( std::move( *fd ) ), expected );
}

//! A sink with no reservation grows as it is written. The slow path, but it must be correct --
//! libarchive does not report an entry's size for every format.
TEST( MemfdSink, UnreservedRoundTrip )
{
	const auto expected { pattern( 40'000, 3 ) };

	auto sink { ipc::MemfdSink::create() };
	ASSERT_TRUE( sink.has_value() );

	ASSERT_TRUE( ( *sink )->write( expected ).has_value() );

	auto fd { ( *sink )->seal() };
	ASSERT_TRUE( fd.has_value() );

	EXPECT_EQ( readBack( std::move( *fd ) ), expected );
}

//! Many small writes assemble in order. This is the extraction loop's actual shape.
TEST( MemfdSink, ManyWritesAppendInOrder )
{
	const auto expected { pattern( 100'000, 7 ) };

	auto sink { ipc::MemfdSink::create() };
	ASSERT_TRUE( sink.has_value() );
	ASSERT_TRUE( ( *sink )->reserve( expected.size() ).has_value() );

	constexpr std::size_t CHUNK { 7919 }; // prime, so the last write is short
	for ( std::size_t offset = 0; offset < expected.size(); offset += CHUNK )
	{
		const auto count { std::min( CHUNK, expected.size() - offset ) };
		ASSERT_TRUE( ( *sink )->write( std::span { expected }.subspan( offset, count ) ).has_value() );
	}

	auto fd { ( *sink )->seal() };
	ASSERT_TRUE( fd.has_value() );

	EXPECT_EQ( readBack( std::move( *fd ) ), expected );
}

//! Over-reserving is trimmed away, so a module that guessed high does not ship trailing zeroes as
//! if they were data.
TEST( MemfdSink, OverReservationIsTrimmed )
{
	const auto expected { pattern( 500, 11 ) };

	auto sink { ipc::MemfdSink::create() };
	ASSERT_TRUE( sink.has_value() );

	ASSERT_TRUE( ( *sink )->reserve( 100'000 ).has_value() );
	ASSERT_TRUE( ( *sink )->write( expected ).has_value() );

	auto fd { ( *sink )->seal() };
	ASSERT_TRUE( fd.has_value() );

	const auto actual { readBack( std::move( *fd ) ) };
	EXPECT_EQ( actual.size(), expected.size() ) << "the reservation was not trimmed to what was written";
	EXPECT_EQ( actual, expected );
}

//! Under-reserving is not an error: reserve is a hint about the expected total, not a limit.
TEST( MemfdSink, UnderReservationStillGrows )
{
	const auto expected { pattern( 20'000, 13 ) };

	auto sink { ipc::MemfdSink::create() };
	ASSERT_TRUE( sink.has_value() );

	ASSERT_TRUE( ( *sink )->reserve( 1024 ).has_value() );
	ASSERT_TRUE( ( *sink )->write( expected ).has_value() );

	auto fd { ( *sink )->seal() };
	ASSERT_TRUE( fd.has_value() );

	EXPECT_EQ( readBack( std::move( *fd ) ), expected );
}

//! A generator that produced nothing yields an empty, valid result rather than a failure.
TEST( MemfdSink, EmptyOutput )
{
	auto sink { ipc::MemfdSink::create() };
	ASSERT_TRUE( sink.has_value() );

	auto fd { ( *sink )->seal() };
	ASSERT_TRUE( fd.has_value() );

	EXPECT_TRUE( readBack( std::move( *fd ) ).empty() );
}

//! finish() seals the object, so what the host maps cannot be changed afterwards.
/** The seal is the reason the host can map the result without copying it defensively. */
TEST( MemfdSink, FinishedOutputIsSealed )
{
	const auto expected { pattern( 4096, 17 ) };

	auto sink { ipc::MemfdSink::create() };
	ASSERT_TRUE( sink.has_value() );
	ASSERT_TRUE( ( *sink )->reserve( expected.size() ).has_value() );
	ASSERT_TRUE( ( *sink )->write( expected ).has_value() );

	auto fd { ( *sink )->seal() };
	ASSERT_TRUE( fd.has_value() );

	const auto seals { ::fcntl( fd->get(), F_GET_SEALS ) };
	ASSERT_GE( seals, 0 );
	EXPECT_TRUE( ( seals & F_SEAL_WRITE ) != 0 );
	EXPECT_TRUE( ( seals & F_SEAL_SHRINK ) != 0 );
	EXPECT_TRUE( ( seals & F_SEAL_GROW ) != 0 );

	constexpr std::byte intruder { 0xFF };
	EXPECT_EQ( ::pwrite( fd->get(), &intruder, 1, 0 ), -1 ) << "a sealed result accepted a write";
}
