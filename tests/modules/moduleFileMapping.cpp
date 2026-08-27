#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <numeric>
#include <vector>

#include "ModuleFile.hpp"
#include "ipc/Blob.hpp"
#include "ipc/BlobFile.hpp"
#include "psd.hpp"

//! A ModuleFile that only implements read(), the way a transport with no mapping would.
class ReadOnlyFile final : public idhan::ModuleFile
{
	std::span< const std::byte > m_bytes;

  public:

	explicit ReadOnlyFile( const std::span< const std::byte > bytes ) : m_bytes( bytes ) {}

	[[nodiscard]] std::size_t size() const override { return m_bytes.size(); }

	[[nodiscard]] std::expected< std::size_t, idhan::ModuleError > read(
		const std::span< std::byte > out,
		const std::size_t offset ) const override
	{
		if ( offset >= m_bytes.size() ) return 0;

		const std::size_t count { std::min( m_bytes.size() - offset, out.size() ) };
		std::memcpy( out.data(), m_bytes.data() + offset, count );
		return count;
	}
};

[[nodiscard]] static std::vector< std::byte > countingBytes( const std::size_t count )
{
	std::vector< std::byte > bytes( count );
	for ( std::size_t i = 0; i < count; ++i ) bytes[ i ] = static_cast< std::byte >( i & 0xFF );
	return bytes;
}

TEST_CASE( "ModuleFile::mapped exposes the transport's own memory", "[modules][file]" )
{
	const auto source { countingBytes( 8192 ) };

	SECTION( "a file over borrowed memory hands back that same memory" )
	{
		const auto file { idhan::ModuleFile::fromBytes( source ) };

		REQUIRE( file->mapped().size() == source.size() );
		// The point of the call: the same address, not a copy of the contents.
		REQUIRE( file->mapped().data() == source.data() );
	}

	SECTION( "a blob hands back its mapping" )
	{
		auto blob { idhan::ipc::Blob::fromBytes( source ) };
		REQUIRE( blob.has_value() );

		const auto mapping { blob->bytes().data() };
		const idhan::ipc::BlobFile file { std::move( *blob ) };

		REQUIRE( file.mapped().size() == source.size() );
		REQUIRE( file.mapped().data() == mapping );
		// It is a distinct mapping of a copy, so it must not alias the source it was built from.
		REQUIRE( file.mapped().data() != source.data() );
		REQUIRE( std::equal( file.mapped().begin(), file.mapped().end(), source.begin() ) );
	}

	SECTION( "a transport without a mapping says so rather than pretending" )
	{
		const ReadOnlyFile file { source };

		REQUIRE( file.size() == source.size() );
		REQUIRE( file.mapped().empty() );
	}
}

TEST_CASE( "openWholeFile borrows a mapping and copies only without one", "[modules][psd]" )
{
	const auto source { countingBytes( 64 * 1024 ) };

	SECTION( "borrows when the file is mapped" )
	{
		const auto file { idhan::ModuleFile::fromBytes( source ) };

		const auto contents { psd::openWholeFile( *file ) };
		REQUIRE( contents.has_value() );

		REQUIRE( contents->size() == source.size() );
		REQUIRE( static_cast< const void* >( contents->data() ) == static_cast< const void* >( source.data() ) );
	}

	SECTION( "copies when it is not, and the bytes still match" )
	{
		const ReadOnlyFile file { source };

		const auto contents { psd::openWholeFile( file ) };
		REQUIRE( contents.has_value() );

		REQUIRE( contents->size() == source.size() );
		REQUIRE( static_cast< const void* >( contents->data() ) != static_cast< const void* >( source.data() ) );
		REQUIRE( std::memcmp( contents->data(), source.data(), source.size() ) == 0 );
	}

	SECTION( "an empty file is not an error on either path" )
	{
		const ReadOnlyFile file { {} };

		const auto contents { psd::openWholeFile( file ) };
		REQUIRE( contents.has_value() );
		REQUIRE( contents->size() == 0 );
	}
}
