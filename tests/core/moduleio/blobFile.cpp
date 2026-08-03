//
// Created by kj16609 on 8/2/26.
//

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <fcntl.h>
#include <format>
#include <filesystem>
#include <random>
#include <unistd.h>
#include <vector>

#include "ipc/Blob.hpp"
#include "ipc/BlobFile.hpp"
#include "ipc/UniqueFd.hpp"

using namespace idhan;

namespace
{

//! A temporary file holding \p size deterministic bytes, removed when it goes out of scope.
/** Deterministic rather than random so a failure names a byte the test can recompute, and so a
 *  reproduction does not depend on a seed being logged. */
class TempFile
{
	std::filesystem::path m_path {};

  public:

	std::vector< std::byte > m_expected {};

	explicit TempFile( const std::size_t size )
	{
		m_path = std::filesystem::temp_directory_path()
		       / std::format( "idhan-blobfile-{}-{}", ::getpid(), size );

		m_expected.resize( size );
		for ( std::size_t i = 0; i < size; ++i )
			m_expected[ i ] = static_cast< std::byte >( ( i * 31u + ( i >> 8 ) ) & 0xFFu );

		const ipc::UniqueFd fd { ::open( m_path.c_str(), O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC, 0600 ) };
		EXPECT_TRUE( static_cast< bool >( fd ) );

		std::size_t written { 0 };
		while ( written < size )
		{
			const auto put { ::pwrite(
				fd.get(), m_expected.data() + written, size - written, static_cast< off_t >( written ) ) };
			// EXPECT rather than ASSERT: this is a constructor, and ASSERT_* returns void.
			EXPECT_GT( put, 0 );
			if ( put <= 0 ) break;
			written += static_cast< std::size_t >( put );
		}
	}

	~TempFile() { std::filesystem::remove( m_path ); }

	TempFile( const TempFile& ) = delete;
	TempFile& operator=( const TempFile& ) = delete;
	TempFile( TempFile&& ) = delete;
	TempFile& operator=( TempFile&& ) = delete;

	[[nodiscard]] const std::filesystem::path& path() const { return m_path; }

	//! The file as a worker receives it: a descriptor, mapped, with the descriptor dropped.
	[[nodiscard]] ipc::Blob mapAsWorkerWould() const
	{
		ipc::UniqueFd fd { ::open( m_path.c_str(), O_RDONLY | O_CLOEXEC ) };
		EXPECT_TRUE( static_cast< bool >( fd ) );

		auto blob { ipc::Blob::adopt( std::move( fd ) ) };
		EXPECT_TRUE( blob.has_value() );

		return std::move( *blob );
	}
};

//! Sizes spanning every boundary that has ever hidden an off-by-one here: empty, single byte, either
//! side of a page, and several pages.
const std::vector< std::size_t > SIZES { 0, 1, 4095, 4096, 4097, 65536, 1024 * 1024 + 7 };

} // namespace

//! A whole-file read matches the file, at every size.
TEST( BlobFile, ReadsWholeFile )
{
	for ( const auto size : SIZES )
	{
		const TempFile file { size };
		ipc::BlobFile handle { file.mapAsWorkerWould() };

		ASSERT_EQ( handle.size(), size ) << "size " << size;

		std::vector< std::byte > out( size );
		const auto count { handle.read( out, 0 ) };

		ASSERT_TRUE( count.has_value() ) << "size " << size;
		EXPECT_EQ( *count, size ) << "size " << size;
		EXPECT_EQ( out, file.m_expected ) << "size " << size;
	}
}

//! Reading in pieces at arbitrary offsets reassembles the file. This is what every converted module
//! actually does -- none of them read the whole thing in one call.
TEST( BlobFile, ReadsRangesAtOffsets )
{
	const TempFile file { 65536 + 123 };
	ipc::BlobFile handle { file.mapAsWorkerWould() };

	// Deliberately not a divisor of the size, so the final read is short.
	constexpr std::size_t CHUNK { 4093 };

	std::vector< std::byte > assembled {};
	std::vector< std::byte > buffer( CHUNK );

	for ( std::size_t offset = 0; offset < handle.size(); )
	{
		const auto count { handle.read( buffer, offset ) };
		ASSERT_TRUE( count.has_value() );
		ASSERT_GT( *count, 0u ) << "stalled at offset " << offset;

		assembled.insert( assembled.end(), buffer.begin(), buffer.begin() + static_cast< std::ptrdiff_t >( *count ) );
		offset += *count;
	}

	EXPECT_EQ( assembled, file.m_expected );
}

//! Reading past the end yields 0 rather than an error.
/** Load-bearing, not cosmetic: a demuxer probing beyond the end for a trailer is normal, and the
 *  FFmpeg and vips sources both treat a zero return as end-of-file. An error here would surface as
 *  an unreadable video rather than as anything resembling its cause. */
TEST( BlobFile, ReadPastEndReturnsZero )
{
	const TempFile file { 4096 };
	ipc::BlobFile handle { file.mapAsWorkerWould() };

	std::vector< std::byte > out( 64 );

	const auto at_end { handle.read( out, handle.size() ) };
	ASSERT_TRUE( at_end.has_value() );
	EXPECT_EQ( *at_end, 0u );

	const auto beyond { handle.read( out, handle.size() + 4096 ) };
	ASSERT_TRUE( beyond.has_value() );
	EXPECT_EQ( *beyond, 0u );
}

//! A read straddling the end returns only what exists, and does not report an error.
TEST( BlobFile, ReadStraddlingEndIsShort )
{
	const TempFile file { 100 };
	ipc::BlobFile handle { file.mapAsWorkerWould() };

	std::vector< std::byte > out( 64 );
	const auto count { handle.read( out, 80 ) };

	ASSERT_TRUE( count.has_value() );
	EXPECT_EQ( *count, 20u );
	EXPECT_TRUE( std::equal( out.begin(), out.begin() + 20, file.m_expected.begin() + 80 ) );
}

//! An empty output buffer reads nothing and is not an error.
TEST( BlobFile, EmptyBufferReadsNothing )
{
	const TempFile file { 4096 };
	ipc::BlobFile handle { file.mapAsWorkerWould() };

	const auto count { handle.read( {}, 0 ) };
	ASSERT_TRUE( count.has_value() );
	EXPECT_EQ( *count, 0u );
}

//! Dropping the descriptor keeps the mapping usable.
/** This is what the worker does to its call input, so that /proc/self/fd names nothing. If the
 *  mapping did not outlive the descriptor every module call would fail, so the test is really
 *  asserting that the mitigation is free. */
TEST( BlobFile, SurvivesDescriptorClosure )
{
	const TempFile file { 8192 };

	auto blob { file.mapAsWorkerWould() };
	ASSERT_TRUE( blob.valid() );

	blob.closeDescriptor();
	EXPECT_TRUE( blob.valid() ) << "a blob with a mapping but no descriptor still holds its bytes";
	EXPECT_LT( blob.fd(), 0 );

	const ipc::BlobFile handle { std::move( blob ) };

	std::vector< std::byte > out( file.m_expected.size() );
	const auto count { handle.read( out, 0 ) };

	ASSERT_TRUE( count.has_value() );
	EXPECT_EQ( *count, file.m_expected.size() );
	EXPECT_EQ( out, file.m_expected );
}

//! Nothing is copied: the mapping is of the file itself, so it tracks the page cache rather than a
//! snapshot taken at open time.
/** Also the closest this suite gets to asserting the memory property the whole design exists for.
 *  A memfd copy would return the old bytes here. */
TEST( BlobFile, MapsTheFileRatherThanACopy )
{
	const TempFile file { 4096 };
	const ipc::BlobFile handle { file.mapAsWorkerWould() };

	// Rewrite the first byte through a separate descriptor, after the mapping exists.
	{
		const ipc::UniqueFd writer { ::open( file.path().c_str(), O_WRONLY | O_CLOEXEC ) };
		ASSERT_TRUE( static_cast< bool >( writer ) );
		constexpr std::byte marker { 0xAB };
		ASSERT_EQ( ::pwrite( writer.get(), &marker, 1, 0 ), 1 );
	}

	std::vector< std::byte > out( 1 );
	const auto count { handle.read( out, 0 ) };

	ASSERT_TRUE( count.has_value() );
	ASSERT_EQ( *count, 1u );
	EXPECT_EQ( out[ 0 ], std::byte { 0xAB } );
}
