#pragma once

#include <json/json.h>

#include <filesystem>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace idhan::hydrus::ptr
{

enum class UpdateType
{
	Unknown,
	Definitions,
	Content,
	Metadata
};

struct DefinitionsUpdate
{
	std::unordered_map< int, std::string > hash_ids_to_hashes; // service_hash_id → hex(SHA-256)
	std::unordered_map< int, std::string > tag_ids_to_tags; // service_tag_id → "namespace:subtag"
};

struct ContentUpdateMapping
{
	int tag_id;
	std::vector< int > hash_ids;
};

struct ContentUpdate
{
	std::vector< ContentUpdateMapping > mappings_add;
	std::vector< std::pair< int, int > > tag_parents_add; // (child_id, parent_id)
	std::vector< std::pair< int, int > > tag_siblings_add; // (bad_id, good_id)
};

struct MetadataUpdateEntry
{
	int index;
	std::vector< std::string > hashes;
	int64_t begin;
	int64_t end;
};

struct MetadataUpdate
{
	std::vector< MetadataUpdateEntry > updates;
	int64_t next_update_due;
};

using ParsedUpdate = std::variant< DefinitionsUpdate, ContentUpdate, MetadataUpdate >;

// Read a file from disk into a byte buffer
std::vector< char > readFile( const std::filesystem::path& path );

// Detect the type of an update from its decompressed JSON content
UpdateType detectUpdateType( const Json::Value& root );

// Decompress zlib-compressed bytes and return the JSON root
Json::Value decompressToJson( const std::vector< char >& compressed_data );

// Full pipeline: read file → decompress → parse → return typed data
ParsedUpdate parseUpdateFile( const std::filesystem::path& path );

// Parse from already-loaded bytes
ParsedUpdate parseUpdateBytes( const std::vector< char >& compressed_data );

// Parse from already-decompressed JSON root
ParsedUpdate parseUpdateJson( const Json::Value& root );

// Type-specific parsers
DefinitionsUpdate parseDefinitionsUpdate( const Json::Value& serialisable_info );
ContentUpdate parseContentUpdate( const Json::Value& serialisable_info );
MetadataUpdate parseMetadataUpdate( const Json::Value& serialisable_info );

} // namespace idhan::hydrus::ptr
