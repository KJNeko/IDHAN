#include "ptr/flatten/Manifest.hpp"

#include <json/json.h>

#include <spdlog/spdlog.h>

#include <format>
#include <fstream>
#include <stdexcept>

namespace idhan::hydrus::ptr
{

//! \throws std::runtime_error if \p key is absent. Every field this build writes is required on
//!         read: there is no backwards compatibility, so a missing key means a manifest from an
//!         older format, and defaulting it would silently misreport what the run actually did.
const Json::Value& require( const Json::Value& json, const char* const key )
{
	if ( !json.isObject() || !json.isMember( key ) )
		throw std::runtime_error(
			std::format( "Manifest is missing \"{}\"; the directory must be re-flattened", key ) );

	return json[ key ];
}

Json::Value statsToJson( const FlattenStats& stats )
{
	Json::Value json { Json::objectValue };
	json[ "events_scanned" ] = static_cast< Json::UInt64 >( stats.events_scanned );
	json[ "mappings_after_collapse" ] = static_cast< Json::UInt64 >( stats.mappings_after_collapse );
	json[ "events_collapsed" ] = static_cast< Json::UInt64 >( stats.events_collapsed );
	json[ "terminal_deletes" ] = static_cast< Json::UInt64 >( stats.terminal_deletes );
	json[ "terminal_delete_records" ] = static_cast< Json::UInt64 >( stats.terminal_delete_records );
	json[ "skipped_files" ] = static_cast< Json::UInt64 >( stats.skipped_files );
	json[ "skipped_missing_definitions" ] = static_cast< Json::UInt64 >( stats.skipped_missing_definitions );
	json[ "defined_tags" ] = static_cast< Json::UInt64 >( stats.defined_tags );
	json[ "used_tags" ] = static_cast< Json::UInt64 >( stats.used_tags );
	return json;
}

FlattenStats statsFromJson( const Json::Value& json )
{
	FlattenStats stats {};

	stats.events_scanned = require( json, "events_scanned" ).asUInt64();
	stats.mappings_after_collapse = require( json, "mappings_after_collapse" ).asUInt64();
	stats.events_collapsed = require( json, "events_collapsed" ).asUInt64();
	stats.terminal_deletes = require( json, "terminal_deletes" ).asUInt64();
	stats.terminal_delete_records = require( json, "terminal_delete_records" ).asUInt64();
	stats.skipped_files = require( json, "skipped_files" ).asUInt64();
	stats.skipped_missing_definitions = require( json, "skipped_missing_definitions" ).asUInt64();
	stats.defined_tags = require( json, "defined_tags" ).asUInt64();
	stats.used_tags = require( json, "used_tags" ).asUInt64();
	return stats;
}


void writeManifest( const std::filesystem::path& dir, const CompactManifest& manifest )
{
	Json::Value root { Json::objectValue };
	root[ "format_version" ] = manifest.format_version;
	root[ "first_update_index" ] = manifest.first_update_index;
	root[ "last_update_index" ] = manifest.last_update_index;
	root[ "max_records_per_chunk" ] = static_cast< Json::UInt64 >( manifest.max_records_per_chunk );
	root[ "discard_terminal_deletes" ] = manifest.discard_terminal_deletes;
	root[ "relations_file" ] = manifest.relations_file;
	root[ "stats" ] = statsToJson( manifest.stats );

	// Seeded as an array so an empty chunk list serialises as [] rather than null.
	Json::Value chunks { Json::arrayValue };
	for ( const auto& chunk : manifest.chunks )
	{
		Json::Value entry { Json::objectValue };
		entry[ "file" ] = chunk.file;
		entry[ "records" ] = static_cast< Json::UInt64 >( chunk.records );
		entry[ "mappings" ] = static_cast< Json::UInt64 >( chunk.mappings );
		chunks.append( entry );
	}
	root[ "chunks" ] = chunks;

	std::filesystem::create_directories( dir );

	const auto path = dir / MANIFEST_FILENAME;
	std::ofstream file { path, std::ios::trunc };
	if ( !file ) throw std::runtime_error( std::format( "Failed to open manifest {} for writing", path.string() ) );

	Json::StreamWriterBuilder builder;
	builder[ "indentation" ] = "\t";
	file << Json::writeString( builder, root );

	if ( !file ) throw std::runtime_error( std::format( "Failed to write manifest {}", path.string() ) );
}

CompactManifest readManifest( const std::filesystem::path& dir )
{
	const auto path = dir / MANIFEST_FILENAME;

	std::ifstream file { path };
	if ( !file ) throw std::runtime_error( std::format( "No manifest at {}", path.string() ) );

	Json::Value root;
	Json::CharReaderBuilder builder;
	std::string errors;
	if ( !Json::parseFromStream( builder, file, &root, &errors ) )
		throw std::runtime_error( std::format( "Failed to parse manifest {}: {}", path.string(), errors ) );

	CompactManifest manifest {};
	manifest.format_version = require( root, "format_version" ).asUInt();

	if ( manifest.format_version != CHUNK_FORMAT_VERSION )
		throw std::runtime_error(
			std::format(
				"Manifest at {} is format version {}, this build writes and reads {}. Re-flatten the corpus.",
				path.string(),
				manifest.format_version,
				CHUNK_FORMAT_VERSION ) );

	manifest.first_update_index = require( root, "first_update_index" ).asInt();
	manifest.last_update_index = require( root, "last_update_index" ).asInt();
	manifest.max_records_per_chunk = require( root, "max_records_per_chunk" ).asUInt64();
	manifest.discard_terminal_deletes = require( root, "discard_terminal_deletes" ).asBool();
	manifest.relations_file = require( root, "relations_file" ).asString();
	manifest.stats = statsFromJson( require( root, "stats" ) );

	const auto& chunks = root[ "chunks" ];
	if ( chunks.isArray() )
	{
		for ( const auto& entry : chunks )
		{
			if ( !entry.isObject() ) continue;
			manifest.chunks.push_back( ChunkEntry { entry.get( "file", "" ).asString(),
				                                    entry.get( "records", Json::UInt64( 0 ) ).asUInt64(),
				                                    entry.get( "mappings", Json::UInt64( 0 ) ).asUInt64() } );
		}
	}

	return manifest;
}

bool isCompactedDirectory( const std::filesystem::path& dir )
{
	std::error_code ec;
	if ( !std::filesystem::exists( dir / MANIFEST_FILENAME, ec ) ) return false;

	try
	{
		readManifest( dir );
		return true;
	}
	catch ( const std::exception& e )
	{
		spdlog::warn( "{} holds a manifest this build cannot use: {}", dir.string(), e.what() );
		return false;
	}
}

} // namespace idhan::hydrus::ptr
