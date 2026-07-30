#include "ptr/flatten/Manifest.hpp"

#include <json/json.h>

#include <spdlog/spdlog.h>

#include <format>
#include <fstream>
#include <stdexcept>

namespace idhan::hydrus::ptr
{

namespace
{

Json::Value statsToJson( const FlattenStats& stats )
{
	Json::Value json { Json::objectValue };
	json[ "events_scanned" ] = static_cast< Json::UInt64 >( stats.events_scanned );
	json[ "mappings_after_collapse" ] = static_cast< Json::UInt64 >( stats.mappings_after_collapse );
	json[ "events_collapsed" ] = static_cast< Json::UInt64 >( stats.events_collapsed );
	json[ "terminal_deletes" ] = static_cast< Json::UInt64 >( stats.terminal_deletes );
	json[ "skipped_files" ] = static_cast< Json::UInt64 >( stats.skipped_files );
	json[ "skipped_missing_definitions" ] = static_cast< Json::UInt64 >( stats.skipped_missing_definitions );
	return json;
}

FlattenStats statsFromJson( const Json::Value& json )
{
	FlattenStats stats {};
	if ( !json.isObject() ) return stats;

	stats.events_scanned = json.get( "events_scanned", Json::UInt64( 0 ) ).asUInt64();
	stats.mappings_after_collapse = json.get( "mappings_after_collapse", Json::UInt64( 0 ) ).asUInt64();
	stats.events_collapsed = json.get( "events_collapsed", Json::UInt64( 0 ) ).asUInt64();
	stats.terminal_deletes = json.get( "terminal_deletes", Json::UInt64( 0 ) ).asUInt64();
	stats.skipped_files = json.get( "skipped_files", Json::UInt64( 0 ) ).asUInt64();
	stats.skipped_missing_definitions = json.get( "skipped_missing_definitions", Json::UInt64( 0 ) ).asUInt64();
	return stats;
}

} // namespace

void writeManifest( const std::filesystem::path& dir, const CompactManifest& manifest )
{
	Json::Value root { Json::objectValue };
	root[ "format_version" ] = manifest.format_version;
	root[ "first_update_index" ] = manifest.first_update_index;
	root[ "last_update_index" ] = manifest.last_update_index;
	root[ "max_records_per_chunk" ] = static_cast< Json::UInt64 >( manifest.max_records_per_chunk );
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
	manifest.format_version = root.get( "format_version", 0 ).asUInt();
	manifest.first_update_index = root.get( "first_update_index", 0 ).asInt();
	manifest.last_update_index = root.get( "last_update_index", 0 ).asInt();
	manifest.max_records_per_chunk = root.get( "max_records_per_chunk", Json::UInt64( 0 ) ).asUInt64();
	manifest.relations_file = root.get( "relations_file", "" ).asString();
	manifest.stats = statsFromJson( root[ "stats" ] );

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
	try
	{
		const auto manifest = readManifest( dir );
		if ( manifest.format_version != CHUNK_FORMAT_VERSION )
		{
			spdlog::warn(
				"Manifest at {} is format version {}, this build understands {}",
				dir.string(),
				manifest.format_version,
				CHUNK_FORMAT_VERSION );
			return false;
		}
		return true;
	}
	catch ( const std::exception& e )
	{
		spdlog::debug( "{} is not a compacted directory: {}", dir.string(), e.what() );
		return false;
	}
}

} // namespace idhan::hydrus::ptr
