#pragma once

#include <json/json.h>

#include <filesystem>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace idhan::hydrus::ptr
{

//! Kind of PTR update file (from its decompressed JSON content).
enum class UpdateType
{
	Unknown,
	Definitions,
	Content,
	Metadata
};

//! A PTR "definitions" update: the id→value lookup tables the content updates reference.
struct DefinitionsUpdate
{
	std::unordered_map< int, std::string > hash_ids_to_hashes; // service_hash_id → hex(SHA-256)
	std::unordered_map< int, std::string > tag_ids_to_tags; // service_tag_id → "namespace:subtag"
};

//! A single tag→files mapping within a content update (ids reference a DefinitionsUpdate).
struct ContentUpdateMapping
{
	int tag_id;
	std::vector< int > hash_ids;
};

//! A PTR "content" update: mappings and tag relationships to add and to delete.
struct ContentUpdate
{
	std::vector< ContentUpdateMapping > mappings_add;
	std::vector< std::pair< int, int > > tag_parents_add; // (child_id, parent_id)
	std::vector< std::pair< int, int > > tag_siblings_add; // (bad_id, good_id)

	std::vector< ContentUpdateMapping > mappings_delete;
	std::vector< std::pair< int, int > > tag_parents_delete; // (child_id, parent_id)
	std::vector< std::pair< int, int > > tag_siblings_delete; // (bad_id, good_id)
};

//! One entry in a metadata update: which update files cover which hash range.
struct MetadataUpdateEntry
{
	int index;
	std::vector< std::string > hashes;
	int64_t begin;
	int64_t end;
};

//! A PTR "metadata" update: the index of available update files and when the next is due.
struct MetadataUpdate
{
	std::vector< MetadataUpdateEntry > updates;
	int64_t next_update_due;
};

//! The typed result of parsing a PTR update file (one of the three update kinds).
using ParsedUpdate = std::variant< DefinitionsUpdate, ContentUpdate, MetadataUpdate >;

//! Reads a file from disk into a byte buffer.
std::vector< char > readFile( const std::filesystem::path& path );

//! Detects the update type from an already-decompressed JSON root.
UpdateType detectUpdateType( const Json::Value& root );

//! Decompresses zlib-compressed bytes and returns the parsed JSON root.
Json::Value decompressToJson( const std::vector< char >& compressed_data );

//! Full pipeline: read file → decompress → parse → typed ParsedUpdate.
ParsedUpdate parseUpdateFile( const std::filesystem::path& path );

//! Parses a ParsedUpdate from already-loaded compressed bytes.
ParsedUpdate parseUpdateBytes( const std::vector< char >& compressed_data );

//! Parses a ParsedUpdate from an already-decompressed JSON root.
ParsedUpdate parseUpdateJson( const Json::Value& root );

//! Parses the definitions payload of an update's serialisable info.
DefinitionsUpdate parseDefinitionsUpdate( const Json::Value& serialisable_info );
//! Parses the content payload of an update's serialisable info.
ContentUpdate parseContentUpdate( const Json::Value& serialisable_info );
//! Parses the metadata payload of an update's serialisable info.
MetadataUpdate parseMetadataUpdate( const Json::Value& serialisable_info );

//! Parses the "updates" array of the app's own ptr_metadata.json cache format (object-shaped
//! entries: index/hashes/begin/end), as opposed to parseMetadataUpdate's raw Hydrus array format.
//! An entry whose "hashes" field is missing or not an array is logged and skipped rather than kept
//! with an empty hash list.
MetadataUpdate parseMetadataCacheJson( const Json::Value& root );

} // namespace idhan::hydrus::ptr
