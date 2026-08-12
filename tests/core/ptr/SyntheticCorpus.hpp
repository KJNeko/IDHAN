// Builds real zlib-compressed .ptrupdate files so the scan stage is exercised through the same
// parser the production path uses, rather than against a mocked-out reader.

#pragma once

#include <zlib.h>

#include <json/json.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "ptr/PTRConstants.hpp"
#include "ptr/PTRFileParser.hpp"

namespace idhan::hydrus::ptr::test
{

//! Compresses \p root and writes it to \p dir / (\p hash_hex + ".ptrupdate").
inline void writeUpdateFile( const std::filesystem::path& dir, const std::string& hash_hex, const Json::Value& root )
{
	Json::StreamWriterBuilder builder;
	builder[ "indentation" ] = "";
	const auto text = Json::writeString( builder, root );

	uLongf bound = ::compressBound( static_cast< uLong >( text.size() ) );
	std::vector< char > compressed( bound == 0 ? 1 : bound );

	const auto result = ::compress2(
		reinterpret_cast< Bytef* >( compressed.data() ),
		&bound,
		reinterpret_cast< const Bytef* >( text.data() ),
		static_cast< uLong >( text.size() ),
		Z_DEFAULT_COMPRESSION );

	if ( result != Z_OK ) throw std::runtime_error( "Failed to compress a synthetic update file" );

	compressed.resize( bound );

	std::filesystem::create_directories( dir );
	std::ofstream file { dir / ( hash_hex + ".ptrupdate" ), std::ios::binary | std::ios::trunc };
	file.write( compressed.data(), static_cast< std::streamsize >( compressed.size() ) );
	if ( !file ) throw std::runtime_error( "Failed to write a synthetic update file" );
}

//! Root wrapper: [serialisable_type, version, serialisable_info].
inline Json::Value makeRoot( const int serialisable_type, Json::Value info )
{
	Json::Value root { Json::arrayValue };
	root.append( serialisable_type );
	root.append( 1 );
	root.append( std::move( info ) );
	return root;
}

//! A definitions update carrying the given hash and tag definitions.
inline Json::Value makeDefinitions( const std::vector< std::pair< std::uint32_t, std::string > >& hashes,
                                    const std::vector< std::pair< std::uint32_t, std::string > >& tags )
{
	Json::Value info { Json::arrayValue };

	Json::Value hash_rows { Json::arrayValue };
	for ( const auto& [ id, hex ] : hashes )
	{
		Json::Value row { Json::arrayValue };
		row.append( static_cast< Json::UInt >( id ) );
		row.append( hex );
		hash_rows.append( row );
	}
	Json::Value hash_block { Json::arrayValue };
	hash_block.append( DEFINITIONS_TYPE_HASHES );
	hash_block.append( hash_rows );
	info.append( hash_block );

	Json::Value tag_rows { Json::arrayValue };
	for ( const auto& [ id, text ] : tags )
	{
		Json::Value row { Json::arrayValue };
		row.append( static_cast< Json::UInt >( id ) );
		row.append( text );
		tag_rows.append( row );
	}
	Json::Value tag_block { Json::arrayValue };
	tag_block.append( DEFINITIONS_TYPE_TAGS );
	tag_block.append( tag_rows );
	info.append( tag_block );

	return makeRoot( SERIALISABLE_TYPE_DEFINITIONS_UPDATE, info );
}

//! One (tag_id -> hash_ids) mapping row.
using MappingRow = std::pair< std::uint32_t, std::vector< std::uint32_t > >;

//! A content update. Any of the four lists may be empty.
inline Json::Value makeContent( const std::vector< MappingRow >& mappings_add,
                                const std::vector< MappingRow >& mappings_delete,
                                const std::vector< std::pair< std::uint32_t, std::uint32_t > >& siblings_add = {},
                                const std::vector< std::pair< std::uint32_t, std::uint32_t > >& parents_add = {} )
{
	const auto mappingRows = []( const std::vector< MappingRow >& rows )
	{
		Json::Value out { Json::arrayValue };
		for ( const auto& [ tag_id, hash_ids ] : rows )
		{
			Json::Value hashes { Json::arrayValue };
			for ( const auto hash_id : hash_ids ) hashes.append( static_cast< Json::UInt >( hash_id ) );

			Json::Value row { Json::arrayValue };
			row.append( static_cast< Json::UInt >( tag_id ) );
			row.append( hashes );
			out.append( row );
		}
		return out;
	};

	const auto pairRows = []( const std::vector< std::pair< std::uint32_t, std::uint32_t > >& rows )
	{
		Json::Value out { Json::arrayValue };
		for ( const auto& [ a, b ] : rows )
		{
			Json::Value row { Json::arrayValue };
			row.append( static_cast< Json::UInt >( a ) );
			row.append( static_cast< Json::UInt >( b ) );
			out.append( row );
		}
		return out;
	};

	const auto actionBlock = []( const int action, Json::Value rows )
	{
		Json::Value block { Json::arrayValue };
		block.append( action );
		block.append( std::move( rows ) );
		return block;
	};

	Json::Value info { Json::arrayValue };

	Json::Value mapping_actions { Json::arrayValue };
	if ( !mappings_add.empty() )
		mapping_actions.append( actionBlock( CONTENT_UPDATE_ADD, mappingRows( mappings_add ) ) );
	if ( !mappings_delete.empty() )
		mapping_actions.append( actionBlock( CONTENT_UPDATE_DELETE, mappingRows( mappings_delete ) ) );
	if ( !mapping_actions.empty() )
	{
		Json::Value block { Json::arrayValue };
		block.append( CONTENT_TYPE_MAPPINGS );
		block.append( mapping_actions );
		info.append( block );
	}

	if ( !siblings_add.empty() )
	{
		Json::Value actions { Json::arrayValue };
		actions.append( actionBlock( CONTENT_UPDATE_ADD, pairRows( siblings_add ) ) );
		Json::Value block { Json::arrayValue };
		block.append( CONTENT_TYPE_TAG_SIBLINGS );
		block.append( actions );
		info.append( block );
	}

	if ( !parents_add.empty() )
	{
		Json::Value actions { Json::arrayValue };
		actions.append( actionBlock( CONTENT_UPDATE_ADD, pairRows( parents_add ) ) );
		Json::Value block { Json::arrayValue };
		block.append( CONTENT_TYPE_TAG_PARENTS );
		block.append( actions );
		info.append( block );
	}

	return makeRoot( SERIALISABLE_TYPE_CONTENT_UPDATE, info );
}

//! A 64-character hex string derived from \p seed, usable as both a file name and a hash value.
//!
//! Injective over the whole 32-bit seed range: bytes 0-3 are the seed itself, little-endian.
//! An earlier version used (seed + i) & 0xFF for every byte, which only distinguished seeds
//! modulo 256 -- so fakeHashHex(257) collided with fakeHashHex(1) and a content file silently
//! overwrote the definitions file it depended on.
//!
//! Byte 0 is the seed's low byte, so a test whose seeds are all below 256 can key a decoded
//! hash back to its seed.
inline std::string fakeHashHex( const unsigned seed )
{
	std::string out;
	out.reserve( 64 );

	for ( int i = 0; i < 32; ++i )
	{
		constexpr char DIGITS[] = "0123456789abcdef";

		const auto byte = i < 4 ? ( seed >> ( 8 * static_cast< unsigned >( i ) ) ) & 0xFFu
		                        : ( seed * 31u + static_cast< unsigned >( i ) ) & 0xFFu;

		out.push_back( DIGITS[ ( byte >> 4 ) & 0xF ] );
		out.push_back( DIGITS[ byte & 0xF ] );
	}
	return out;
}

//! A MetadataUpdate listing the given hashes under each update index.
inline MetadataUpdate makeMetadata( const std::vector< std::pair< int, std::vector< std::string > > >& updates )
{
	MetadataUpdate metadata {};
	for ( const auto& [ index, hashes ] : updates )
	{
		MetadataUpdateEntry entry {};
		entry.index = index;
		entry.hashes = hashes;
		entry.begin = 0;
		entry.end = 0;
		metadata.updates.push_back( entry );
	}
	return metadata;
}

} // namespace idhan::hydrus::ptr::test
