#include "ptr/flatten/ChunkFormat.hpp"

#include <zlib.h>

#include <algorithm>
#include <cstring>
#include <format>
#include <fstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace idhan::hydrus::ptr
{

template < typename T >
void appendPod( std::vector< std::byte >& out, const T& value )
{
	const auto* const bytes = reinterpret_cast< const std::byte* >( &value );
	out.insert( out.end(), bytes, bytes + sizeof( T ) );
}

//! Reads a POD from \p body at \p offset, advancing it. Throws rather than reading past the end.
template < typename T >
T takePod( const std::vector< std::byte >& body, std::size_t& offset, const char* const what )
{
	if ( offset + sizeof( T ) > body.size() )
		throw std::runtime_error( std::format( "Chunk truncated while reading {}", what ) );

	T value {};
	std::memcpy( &value, body.data() + offset, sizeof( T ) );
	offset += sizeof( T );
	return value;
}

std::vector< std::byte > deflateBuffer( const std::vector< std::byte >& input )
{
	// compressBound of 0 is still a positive number, but guard the empty case so data() is valid.
	uLongf bound = ::compressBound( static_cast< uLong >( input.size() ) );
	std::vector< std::byte > out( bound == 0 ? 1 : bound );

	const auto result = ::compress2(
		reinterpret_cast< Bytef* >( out.data() ),
		&bound,
		reinterpret_cast< const Bytef* >( input.data() ),
		static_cast< uLong >( input.size() ),
		Z_DEFAULT_COMPRESSION );

	if ( result != Z_OK ) throw std::runtime_error( "zlib compress2 failed while writing a chunk" );

	out.resize( bound );
	return out;
}

std::vector< std::byte > inflateBuffer( const std::vector< std::byte >& input, const std::uint64_t expected_size )
{
	std::vector< std::byte > out( expected_size );
	if ( expected_size == 0 ) return out;

	uLongf produced = static_cast< uLongf >( expected_size );
	const auto result = ::uncompress(
		reinterpret_cast< Bytef* >( out.data() ),
		&produced,
		reinterpret_cast< const Bytef* >( input.data() ),
		static_cast< uLong >( input.size() ) );

	if ( result != Z_OK ) throw std::runtime_error( "zlib uncompress failed while reading a chunk" );
	if ( produced != expected_size )
		throw std::runtime_error(
			std::format( "Chunk body inflated to {} bytes, header said {}", produced, expected_size ) );

	return out;
}


ChunkWriter::ChunkWriter( std::filesystem::path path ) : m_path( std::move( path ) ) {}

ChunkWriter::~ChunkWriter() = default;

void ChunkWriter::addRecord( std::array< std::byte, SHA256_BYTES > sha256,
                             std::vector< std::uint32_t > add_tag_ids,
                             std::vector< std::uint32_t > del_tag_ids )
{
	m_records.push_back( PendingRecord { sha256, std::move( add_tag_ids ), std::move( del_tag_ids ) } );
}

ChunkStats ChunkWriter::finish( const TagLookup& lookup, TagUsageSet* const usage )
{
	if ( m_finished )
		throw std::runtime_error( std::format( "ChunkWriter::finish called twice for {}", m_path.string() ) );
	m_finished = true;

	ChunkStats stats {};
	stats.records = m_records.size();

	// Collect every distinct referenced id, then resolve once per id rather than once per use.
	std::vector< std::uint32_t > distinct;
	for ( const auto& record : m_records )
	{
		distinct.insert( distinct.end(), record.add_tag_ids.begin(), record.add_tag_ids.end() );
		distinct.insert( distinct.end(), record.del_tag_ids.begin(), record.del_tag_ids.end() );
	}
	std::ranges::sort( distinct );
	distinct.erase( std::ranges::unique( distinct ).begin(), distinct.end() );

	// distinct is already ascending, so the table it produces is sorted by ptr_tag_id.
	std::vector< ChunkStringEntry > strings;
	std::unordered_map< std::uint32_t, std::uint32_t > id_to_index;
	strings.reserve( distinct.size() );
	id_to_index.reserve( distinct.size() );

	for ( const auto tag_id : distinct )
	{
		const auto text = lookup( tag_id );
		if ( !text.has_value() ) continue;

		id_to_index.emplace( tag_id, static_cast< std::uint32_t >( strings.size() ) );
		strings.push_back( ChunkStringEntry { tag_id, std::string( *text ) } );

		if ( usage != nullptr ) usage->mark( tag_id );
	}

	const auto remap = [ & ]( const std::vector< std::uint32_t >& ids )
	{
		std::vector< std::uint32_t > out;
		out.reserve( ids.size() );
		for ( const auto id : ids )
		{
			const auto it = id_to_index.find( id );
			if ( it == id_to_index.end() )
			{
				++stats.missing_definitions;
				continue;
			}
			out.push_back( it->second );
			++stats.mappings;
		}
		return out;
	};

	std::vector< std::byte > body;
	body.reserve( m_records.size() * 96 );

	for ( const auto& entry : strings )
	{
		appendPod( body, entry.ptr_tag_id );
		appendPod( body, static_cast< std::uint32_t >( entry.tag.size() ) );
		const auto* const bytes = reinterpret_cast< const std::byte* >( entry.tag.data() );
		body.insert( body.end(), bytes, bytes + entry.tag.size() );
	}

	for ( const auto& record : m_records )
	{
		const auto adds = remap( record.add_tag_ids );
		const auto dels = remap( record.del_tag_ids );

		body.insert( body.end(), record.sha256.begin(), record.sha256.end() );
		appendPod( body, static_cast< std::uint32_t >( adds.size() ) );
		appendPod( body, static_cast< std::uint32_t >( dels.size() ) );
		for ( const auto index : adds ) appendPod( body, index );
		for ( const auto index : dels ) appendPod( body, index );
	}

	ChunkHeader header {};
	header.magic = CHUNK_MAGIC;
	header.version = CHUNK_FORMAT_VERSION;
	header.body_size = body.size();
	header.record_count = static_cast< std::uint32_t >( m_records.size() );
	header.string_count = static_cast< std::uint32_t >( strings.size() );

	const auto compressed = deflateBuffer( body );

	std::ofstream file { m_path, std::ios::binary | std::ios::trunc };
	if ( !file ) throw std::runtime_error( std::format( "Failed to open chunk {} for writing", m_path.string() ) );

	file.write( reinterpret_cast< const char* >( &header ), sizeof( header ) );
	file.write(
		reinterpret_cast< const char* >( compressed.data() ), static_cast< std::streamsize >( compressed.size() ) );
	if ( !file ) throw std::runtime_error( std::format( "Failed to write chunk {}", m_path.string() ) );

	return stats;
}

Chunk readChunk( const std::filesystem::path& path )
{
	std::ifstream file { path, std::ios::binary | std::ios::ate };
	if ( !file ) throw std::runtime_error( std::format( "Failed to open chunk {}", path.string() ) );

	const auto file_size = static_cast< std::uint64_t >( file.tellg() );
	if ( file_size < sizeof( ChunkHeader ) )
		throw std::runtime_error( std::format( "Chunk {} is too small to hold a header", path.string() ) );

	file.seekg( 0 );

	ChunkHeader header {};
	file.read( reinterpret_cast< char* >( &header ), sizeof( header ) );

	if ( header.magic != CHUNK_MAGIC )
		throw std::runtime_error( std::format( "Chunk {} has the wrong magic", path.string() ) );
	if ( header.version != CHUNK_FORMAT_VERSION )
		throw std::runtime_error( std::format(
			"Chunk {} is format version {}, expected {}", path.string(), header.version, CHUNK_FORMAT_VERSION ) );

	std::vector< std::byte > compressed( file_size - sizeof( ChunkHeader ) );
	if ( !compressed.empty() )
		file.read(
			reinterpret_cast< char* >( compressed.data() ), static_cast< std::streamsize >( compressed.size() ) );

	const auto body = inflateBuffer( compressed, header.body_size );

	Chunk chunk {};
	std::size_t offset { 0 };

	chunk.strings.reserve( header.string_count );
	for ( std::uint32_t i = 0; i < header.string_count; ++i )
	{
		ChunkStringEntry entry {};
		entry.ptr_tag_id = takePod< std::uint32_t >( body, offset, "a string table id" );
		const auto length = takePod< std::uint32_t >( body, offset, "a string table length" );

		if ( offset + length > body.size() ) throw std::runtime_error( "Chunk truncated inside the string table" );
		entry.tag.assign( reinterpret_cast< const char* >( body.data() + offset ), length );
		offset += length;

		chunk.strings.push_back( std::move( entry ) );
	}

	const auto takeIndices = [ & ]( const std::uint32_t count )
	{
		std::vector< std::uint32_t > out;
		out.reserve( count );
		for ( std::uint32_t i = 0; i < count; ++i )
			out.push_back( takePod< std::uint32_t >( body, offset, "a tag index" ) );
		return out;
	};

	chunk.records.reserve( header.record_count );
	for ( std::uint32_t i = 0; i < header.record_count; ++i )
	{
		ChunkRecord record {};

		if ( offset + SHA256_BYTES > body.size() ) throw std::runtime_error( "Chunk truncated inside a record hash" );
		std::memcpy( record.sha256.data(), body.data() + offset, SHA256_BYTES );
		offset += SHA256_BYTES;

		const auto add_count = takePod< std::uint32_t >( body, offset, "an add count" );
		const auto del_count = takePod< std::uint32_t >( body, offset, "a delete count" );

		record.add_indices = takeIndices( add_count );
		record.del_indices = takeIndices( del_count );

		chunk.records.push_back( std::move( record ) );
	}

	return chunk;
}

} // namespace idhan::hydrus::ptr
