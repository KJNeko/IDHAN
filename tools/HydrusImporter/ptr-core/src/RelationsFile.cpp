#include "ptr/flatten/RelationsFile.hpp"

#include <zlib.h>

#include <algorithm>
#include <cstring>
#include <format>
#include <fstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

#include "ptr/flatten/CollapseChain.hpp"

namespace idhan::hydrus::ptr
{

template < typename T >
void appendPod( std::vector< std::byte >& out, const T& value )
{
	const auto* const bytes = reinterpret_cast< const std::byte* >( &value );
	out.insert( out.end(), bytes, bytes + sizeof( T ) );
}

template < typename T >
T takePod( const std::vector< std::byte >& body, std::size_t& offset, const char* const what )
{
	if ( offset + sizeof( T ) > body.size() )
		throw std::runtime_error( std::format( "Relations file truncated while reading {}", what ) );

	T value {};
	std::memcpy( &value, body.data() + offset, sizeof( T ) );
	offset += sizeof( T );
	return value;
}

bool relationLess( const RelationEvent& x, const RelationEvent& y )
{
	if ( x.a != y.a ) return x.a < y.a;
	if ( x.b != y.b ) return x.b < y.b;
	if ( x.update_index != y.update_index ) return x.update_index < y.update_index;
	return x.op < y.op;
}


std::vector< CollapsedRelation > collapseRelations( std::vector< RelationEvent > events )
{
	std::ranges::sort( events, relationLess );

	std::vector< CollapsedRelation > out;

	std::size_t start { 0 };
	while ( start < events.size() )
	{
		std::size_t end { start };
		while ( end < events.size() && events[ end ].a == events[ start ].a && events[ end ].b == events[ start ].b )
			++end;

		std::vector< MappingEvent > chain;
		chain.reserve( end - start );
		for ( std::size_t i = start; i < end; ++i )
			chain
				.push_back( MappingEvent { events[ i ].a, events[ i ].b, events[ i ].update_index, events[ i ].op, 0 } );

		if ( const auto collapsed = collapseChain( chain ); collapsed.has_value() )
			out.push_back( CollapsedRelation { events[ start ].a, events[ start ].b, collapsed->op } );

		start = end;
	}

	return out;
}

RelationsFileStats writeRelationsFile( const std::filesystem::path& path,
                                       const std::vector< CollapsedRelation >& parents,
                                       const std::vector< CollapsedRelation >& siblings,
	const TagLookup& lookup,
	TagUsageSet* const usage )
{
	RelationsFileStats stats {};

	std::vector< std::uint32_t > distinct;
	for ( const auto* const list : { &parents, &siblings } )
	{
		for ( const auto& relation : *list )
		{
			distinct.push_back( relation.a );
			distinct.push_back( relation.b );
		}
	}
	std::ranges::sort( distinct );
	distinct.erase( std::ranges::unique( distinct ).begin(), distinct.end() );

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

	const auto encode = [ & ]( const std::vector< CollapsedRelation >& list, std::uint64_t& counter )
	{
		std::vector< RelationRecord > out;
		out.reserve( list.size() );
		for ( const auto& relation : list )
		{
			const auto a = id_to_index.find( relation.a );
			const auto b = id_to_index.find( relation.b );
			if ( a == id_to_index.end() || b == id_to_index.end() )
			{
				++stats.missing_definitions;
				continue;
			}
			out.push_back( RelationRecord { a->second, b->second, relation.op } );
			++counter;
		}
		return out;
	};

	const auto encoded_parents = encode( parents, stats.parents );
	const auto encoded_siblings = encode( siblings, stats.siblings );

	std::vector< std::byte > body;

	for ( const auto& entry : strings )
	{
		appendPod( body, entry.ptr_tag_id );
		appendPod( body, static_cast< std::uint32_t >( entry.tag.size() ) );
		const auto* const bytes = reinterpret_cast< const std::byte* >( entry.tag.data() );
		body.insert( body.end(), bytes, bytes + entry.tag.size() );
	}

	appendPod( body, static_cast< std::uint32_t >( encoded_parents.size() ) );
	appendPod( body, static_cast< std::uint32_t >( encoded_siblings.size() ) );

	for ( const auto* const list : { &encoded_parents, &encoded_siblings } )
	{
		for ( const auto& record : *list )
		{
			appendPod( body, record.a_index );
			appendPod( body, record.b_index );
			appendPod( body, static_cast< std::uint8_t >( record.op ) );
		}
	}

	uLongf bound = ::compressBound( static_cast< uLong >( body.size() ) );
	std::vector< std::byte > compressed( bound == 0 ? 1 : bound );
	if ( ::compress2(
			 reinterpret_cast< Bytef* >( compressed.data() ),
			 &bound,
			 reinterpret_cast< const Bytef* >( body.data() ),
			 static_cast< uLong >( body.size() ),
			 Z_DEFAULT_COMPRESSION )
	     != Z_OK )
		throw std::runtime_error( "zlib compress2 failed while writing the relations file" );
	compressed.resize( bound );

	ChunkHeader header {};
	header.magic = RELATIONS_MAGIC;
	header.version = CHUNK_FORMAT_VERSION;
	header.body_size = body.size();
	header.record_count = static_cast< std::uint32_t >( encoded_parents.size() + encoded_siblings.size() );
	header.string_count = static_cast< std::uint32_t >( strings.size() );

	std::ofstream file { path, std::ios::binary | std::ios::trunc };
	if ( !file ) throw std::runtime_error( std::format( "Failed to open {} for writing", path.string() ) );

	file.write( reinterpret_cast< const char* >( &header ), sizeof( header ) );
	file.write(
		reinterpret_cast< const char* >( compressed.data() ), static_cast< std::streamsize >( compressed.size() ) );
	if ( !file ) throw std::runtime_error( std::format( "Failed to write {}", path.string() ) );

	return stats;
}

RelationsFile readRelationsFile( const std::filesystem::path& path )
{
	std::ifstream file { path, std::ios::binary | std::ios::ate };
	if ( !file ) throw std::runtime_error( std::format( "Failed to open {}", path.string() ) );

	const auto file_size = static_cast< std::uint64_t >( file.tellg() );
	if ( file_size < sizeof( ChunkHeader ) )
		throw std::runtime_error( std::format( "{} is too small to hold a header", path.string() ) );

	file.seekg( 0 );

	ChunkHeader header {};
	file.read( reinterpret_cast< char* >( &header ), sizeof( header ) );

	if ( header.magic != RELATIONS_MAGIC )
		throw std::runtime_error( std::format( "{} is not a relations file", path.string() ) );
	if ( header.version != CHUNK_FORMAT_VERSION )
		throw std::runtime_error(
			std::format(
				"{} is format version {}, expected {}", path.string(), header.version, CHUNK_FORMAT_VERSION ) );

	std::vector< std::byte > compressed( file_size - sizeof( ChunkHeader ) );
	if ( !compressed.empty() )
		file.read(
			reinterpret_cast< char* >( compressed.data() ), static_cast< std::streamsize >( compressed.size() ) );

	std::vector< std::byte > body( header.body_size );
	if ( header.body_size > 0 )
	{
		uLongf produced = static_cast< uLongf >( header.body_size );
		if ( ::uncompress(
				 reinterpret_cast< Bytef* >( body.data() ),
				 &produced,
				 reinterpret_cast< const Bytef* >( compressed.data() ),
				 static_cast< uLong >( compressed.size() ) )
		     != Z_OK )
			throw std::runtime_error( "zlib uncompress failed while reading the relations file" );
	}

	RelationsFile out {};
	std::size_t offset { 0 };

	out.strings.reserve( header.string_count );
	for ( std::uint32_t i = 0; i < header.string_count; ++i )
	{
		ChunkStringEntry entry {};
		entry.ptr_tag_id = takePod< std::uint32_t >( body, offset, "a string table id" );
		const auto length = takePod< std::uint32_t >( body, offset, "a string table length" );

		if ( offset + length > body.size() )
			throw std::runtime_error( "Relations file truncated inside the string table" );
		entry.tag.assign( reinterpret_cast< const char* >( body.data() + offset ), length );
		offset += length;

		out.strings.push_back( std::move( entry ) );
	}

	const auto parent_count = takePod< std::uint32_t >( body, offset, "the parent count" );
	const auto sibling_count = takePod< std::uint32_t >( body, offset, "the sibling count" );

	const auto takeRecords = [ & ]( const std::uint32_t count )
	{
		std::vector< RelationRecord > records;
		records.reserve( count );
		for ( std::uint32_t i = 0; i < count; ++i )
		{
			RelationRecord record {};
			record.a_index = takePod< std::uint32_t >( body, offset, "a relation a index" );
			record.b_index = takePod< std::uint32_t >( body, offset, "a relation b index" );
			record.op = static_cast< EventOp >( takePod< std::uint8_t >( body, offset, "a relation op" ) );
			records.push_back( record );
		}
		return records;
	};

	out.parents = takeRecords( parent_count );
	out.siblings = takeRecords( sibling_count );

	return out;
}

} // namespace idhan::hydrus::ptr
