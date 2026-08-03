#include "ptr/flatten/DefinitionStore.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <spdlog/spdlog.h>

#include <cstring>
#include <format>
#include <limits>
#include <stdexcept>

namespace idhan::hydrus::ptr
{

namespace
{

#pragma pack( push, 1 )

//! One tags.idx slot. A zero length means the id was never defined.
struct TagIndexEntry
{
	std::uint32_t offset;
	std::uint32_t length;
};

#pragma pack( pop )

static_assert( sizeof( TagIndexEntry ) == 8, "tags.idx stride is format" );

int openForWrite( const std::filesystem::path& path )
{
	const int fd = ::open( path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644 );
	if ( fd < 0 ) throw std::runtime_error( std::format( "Failed to open {} for writing", path.string() ) );
	return fd;
}

void writeAt( const int fd, const void* const data, const std::size_t size, const std::uint64_t offset )
{
	std::size_t written { 0 };
	while ( written < size )
	{
		const auto result = ::pwrite(
			fd,
			static_cast< const std::byte* >( data ) + written,
			size - written,
			static_cast< off_t >( offset + written ) );
		if ( result <= 0 ) throw std::runtime_error( "pwrite failed while writing definitions" );
		written += static_cast< std::size_t >( result );
	}
}

std::optional< unsigned > hexNibble( const char c )
{
	if ( c >= '0' && c <= '9' ) return static_cast< unsigned >( c - '0' );
	if ( c >= 'a' && c <= 'f' ) return static_cast< unsigned >( c - 'a' + 10 );
	if ( c >= 'A' && c <= 'F' ) return static_cast< unsigned >( c - 'A' + 10 );
	return std::nullopt;
}

} // namespace

std::optional< std::array< std::byte, SHA256_BYTES > > decodeSha256Hex( const std::string_view hex )
{
	if ( hex.size() != SHA256_BYTES * 2 ) return std::nullopt;

	std::array< std::byte, SHA256_BYTES > out {};
	for ( std::size_t i = 0; i < SHA256_BYTES; ++i )
	{
		const auto high = hexNibble( hex[ i * 2 ] );
		const auto low = hexNibble( hex[ i * 2 + 1 ] );
		if ( !high.has_value() || !low.has_value() ) return std::nullopt;
		out[ i ] = static_cast< std::byte >( ( *high << 4 ) | *low );
	}
	return out;
}

DefinitionWriter::DefinitionWriter( const std::filesystem::path& dir )
{
	std::filesystem::create_directories( dir );
	m_hashes_fd = openForWrite( dir / HASHES_FILENAME );
	m_tag_index_fd = openForWrite( dir / TAG_INDEX_FILENAME );
	m_tag_blob_fd = openForWrite( dir / TAG_BLOB_FILENAME );
}

DefinitionWriter::~DefinitionWriter()
{
	if ( m_hashes_fd >= 0 ) ::close( m_hashes_fd );
	if ( m_tag_index_fd >= 0 ) ::close( m_tag_index_fd );
	if ( m_tag_blob_fd >= 0 ) ::close( m_tag_blob_fd );
}

bool DefinitionWriter::writeHash( const std::uint32_t hash_id, const std::string_view sha256_hex )
{
	const auto decoded = decodeSha256Hex( sha256_hex );
	if ( !decoded.has_value() )
	{
		spdlog::warn( "Rejecting malformed hash definition for hash_id={} ({} chars)", hash_id, sha256_hex.size() );
		m_rejected_hashes.fetch_add( 1, std::memory_order_relaxed );
		return false;
	}

	// Nothing shared to guard: the offset comes from hash_id, so two threads writing different
	// hashes cannot overlap, and two writing the same hash write the same 32 bytes.
	writeAt( m_hashes_fd, decoded->data(), SHA256_BYTES, static_cast< std::uint64_t >( hash_id ) * SHA256_BYTES );
	return true;
}

void DefinitionWriter::writeTag( const std::uint32_t tag_id, const std::string_view tag )
{
	if ( tag.empty() ) return;

	// The lock covers reserving a blob range and nothing else. Copying the text into the file is
	// the expensive half and needs no exclusion once the range belongs to this caller.
	std::uint64_t offset { 0 };
	{
		std::lock_guard< std::mutex > lock { m_blob_mutex };

		if ( m_blob_offset + tag.size() > std::numeric_limits< std::uint32_t >::max() )
			throw std::runtime_error( "tags.blob exceeded 4 GB; TagIndexEntry offsets are 32-bit" );

		offset = m_blob_offset;
		m_blob_offset += tag.size();
	}

	writeAt( m_tag_blob_fd, tag.data(), tag.size(), offset );

	// Written second so the index never points at bytes that are not there yet. Nothing reads the
	// store until the scan's writers have joined, but the ordering costs nothing and keeps the
	// files consistent if a run dies partway.
	const TagIndexEntry entry { static_cast< std::uint32_t >( offset ), static_cast< std::uint32_t >( tag.size() ) };
	writeAt( m_tag_index_fd, &entry, sizeof( entry ), static_cast< std::uint64_t >( tag_id ) * sizeof( entry ) );
}

DefinitionReader::Mapping DefinitionReader::mapFile( const std::filesystem::path& path )
{
	std::error_code ec;
	const auto size = std::filesystem::file_size( path, ec );
	if ( ec || size == 0 ) return {};

	const int fd = ::open( path.c_str(), O_RDONLY );
	if ( fd < 0 ) throw std::runtime_error( std::format( "Failed to open {} for reading", path.string() ) );

	void* const data = ::mmap( nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0 );
	::close( fd );

	if ( data == MAP_FAILED ) throw std::runtime_error( std::format( "Failed to mmap {}", path.string() ) );

	return Mapping { static_cast< const std::byte* >( data ), size };
}

void DefinitionReader::unmapFile( Mapping& mapping )
{
	if ( mapping.data != nullptr ) ::munmap( const_cast< std::byte* >( mapping.data ), mapping.size );
	mapping = {};
}

DefinitionReader::DefinitionReader( const std::filesystem::path& dir ) :
  m_hashes( mapFile( dir / HASHES_FILENAME ) ),
  m_tag_index( mapFile( dir / TAG_INDEX_FILENAME ) ),
  m_tag_blob( mapFile( dir / TAG_BLOB_FILENAME ) )
{}

DefinitionReader::~DefinitionReader()
{
	unmapFile( m_hashes );
	unmapFile( m_tag_index );
	unmapFile( m_tag_blob );
}

std::optional< std::span< const std::byte > > DefinitionReader::hash( const std::uint32_t hash_id ) const
{
	const auto offset = static_cast< std::uint64_t >( hash_id ) * SHA256_BYTES;
	if ( offset + SHA256_BYTES > m_hashes.size ) return std::nullopt;

	const std::span< const std::byte > slot { m_hashes.data + offset, SHA256_BYTES };

	// A sparse hole reads as zeros, which is how an undefined id is encoded. A real SHA-256 of
	// all zeros is not something PTR will ever contain.
	for ( const auto byte : slot )
	{
		if ( byte != std::byte { 0 } ) return slot;
	}
	return std::nullopt;
}

std::optional< std::string_view > DefinitionReader::tag( const std::uint32_t tag_id ) const
{
	const auto offset = static_cast< std::uint64_t >( tag_id ) * sizeof( TagIndexEntry );
	if ( offset + sizeof( TagIndexEntry ) > m_tag_index.size ) return std::nullopt;

	TagIndexEntry entry {};
	std::memcpy( &entry, m_tag_index.data + offset, sizeof( entry ) );

	if ( entry.length == 0 ) return std::nullopt;
	if ( static_cast< std::uint64_t >( entry.offset ) + entry.length > m_tag_blob.size ) return std::nullopt;

	return std::string_view( reinterpret_cast< const char* >( m_tag_blob.data + entry.offset ), entry.length );
}

} // namespace idhan::hydrus::ptr
