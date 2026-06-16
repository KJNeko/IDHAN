#include "PTRFileParser.hpp"

#include <fstream>
#include <stdexcept>

#include <zlib.h>

#include <json/json.h>

#include <spdlog/spdlog.h>

#include "PTRConstants.hpp"

namespace idhan::hydrus::ptr
{

std::vector< char > readFile( const std::filesystem::path& path )
{
	spdlog::trace( "Reading file: {} ", path.string() );

	std::ifstream file( path, std::ios::binary | std::ios::ate );
	if ( !file )
	{
		spdlog::error( "Failed to open file: {} ", path.string() );
		throw std::runtime_error( "Failed to open file: " + path.string() );
	}

	const auto size = file.tellg();
	file.seekg( 0 );

	std::vector< char > data( size );
	if ( !file.read( data.data(), size ) )
	{
		spdlog::error( "Failed to read {} bytes from file: {}", static_cast< std::streamsize >( size ), path.string() );
		throw std::runtime_error( "Failed to read file: " + path.string() );
	}

	spdlog::trace( "Read {} bytes from {}", static_cast< std::streamsize >( size ), path.string() );
	return data;
}

Json::Value decompressToJson( const std::vector< char >& compressed_data )
{
	spdlog::trace( "Decompressing {} bytes of zlib data", compressed_data.size() );

	z_stream strm {};
	strm.zalloc = Z_NULL;
	strm.zfree = Z_NULL;
	strm.opaque = Z_NULL;
	strm.next_in = reinterpret_cast< Bytef* >( const_cast< char* >( compressed_data.data() ) );
	strm.avail_in = compressed_data.size();

	if ( inflateInit2( &strm, 15 ) != Z_OK )
	{
		spdlog::error( "zlib inflateInit failed" );
		throw std::runtime_error( "zlib inflateInit failed" );
	}

	std::vector< char > decompressed;
	constexpr std::size_t CHUNK_SIZE { 65536 };
	decompressed.resize( CHUNK_SIZE );
	int ret;

	do
	{
		strm.next_out = reinterpret_cast< Bytef* >( decompressed.data() + strm.total_out );
		strm.avail_out = decompressed.size() - strm.total_out;

		if ( strm.avail_out == 0 )
		{
			decompressed.resize( decompressed.size() + CHUNK_SIZE );
			strm.next_out = reinterpret_cast< Bytef* >( decompressed.data() + strm.total_out );
			strm.avail_out = CHUNK_SIZE;
		}

		ret = inflate( &strm, Z_NO_FLUSH );
	} while ( ret == Z_OK );

	decompressed.resize( strm.total_out );
	inflateEnd( &strm );

	if ( ret != Z_STREAM_END )
	{
		spdlog::error( "zlib decompression failed with code {}", ret );
		throw std::runtime_error( "zlib decompression failed" );
	}

	spdlog::trace( "Decompressed to {} bytes", decompressed.size() );

	const std::string json_str( decompressed.data(), decompressed.size() );

	Json::Value root;
	Json::CharReaderBuilder reader_builder;
	std::string parse_errors;

	const auto stream = std::unique_ptr< Json::CharReader >( reader_builder.newCharReader() );
	if ( !stream->parse( json_str.data(), json_str.data() + json_str.size(), &root, &parse_errors ) )
	{
		spdlog::error( "JSON parse error: {}", parse_errors );
		throw std::runtime_error( "JSON parse error: " + parse_errors );
	}

	return root;
}

UpdateType detectUpdateType( const Json::Value& root )
{
	if ( !root.isArray() || root.size() < 2 ) return UpdateType::Unknown;

	const auto serialisable_type = root[ 0 ].asInt();

	switch ( serialisable_type )
	{
		case SERIALISABLE_TYPE_DEFINITIONS_UPDATE:
			return UpdateType::Definitions;
		case SERIALISABLE_TYPE_CONTENT_UPDATE:
			return UpdateType::Content;
		case SERIALISABLE_TYPE_METADATA:
			return UpdateType::Metadata;
		default:
			return UpdateType::Unknown;
	}
}

DefinitionsUpdate parseDefinitionsUpdate( const Json::Value& serialisable_info )
{
	DefinitionsUpdate result;

	if ( !serialisable_info.isArray() )
	{
		spdlog::warn( "Definitions update serialisable_info is not an array" );
		return result;
	}

	for ( const auto& entry : serialisable_info )
	{
		if ( !entry.isArray() || entry.size() < 2 ) continue;

		const auto definition_type = entry[ 0 ].asInt();
		const auto& definitions = entry[ 1 ];

		if ( !definitions.isArray() ) continue;

		if ( definition_type == DEFINITIONS_TYPE_HASHES )
		{
			spdlog::trace( "Parsing hash definitions block" );
			for ( const auto& def : definitions )
			{
				if ( !def.isArray() || def.size() < 2 ) continue;
				const auto hash_id = def[ 0 ].asInt();
				const auto hash_hex = def[ 1 ].asString();
				spdlog::trace( "  hash_id={} -> {}", hash_id, hash_hex );
				result.hash_ids_to_hashes.emplace( hash_id, hash_hex );
			}
		}
		else if ( definition_type == DEFINITIONS_TYPE_TAGS )
		{
			spdlog::trace( "Parsing tag definitions block" );
			for ( const auto& def : definitions )
			{
				if ( !def.isArray() || def.size() < 2 ) continue;
				const auto tag_id = def[ 0 ].asInt();
				const auto tag = def[ 1 ].asString();
				spdlog::trace( "  tag_id={} -> {}", tag_id, tag );
				result.tag_ids_to_tags.emplace( tag_id, tag );
			}
		}
	}

	spdlog::debug(
		"Parsed definitions: {} hashes, {} tags",
		result.hash_ids_to_hashes.size(),
		result.tag_ids_to_tags.size() );

	return result;
}

ContentUpdate parseContentUpdate( const Json::Value& serialisable_info )
{
	ContentUpdate result;

	if ( !serialisable_info.isArray() )
	{
		spdlog::warn( "Content update serialisable_info is not an array" );
		return result;
	}

	for ( const auto& content_entry : serialisable_info )
	{
		if ( !content_entry.isArray() || content_entry.size() < 2 ) continue;

		const auto content_type = content_entry[ 0 ].asInt();
		const auto& actions_to_datas = content_entry[ 1 ];

		if ( !actions_to_datas.isArray() ) continue;

		for ( const auto& action_entry : actions_to_datas )
		{
			if ( !action_entry.isArray() || action_entry.size() < 2 ) continue;

			const auto action = action_entry[ 0 ].asInt();
			if ( action != CONTENT_UPDATE_ADD && action != CONTENT_UPDATE_DELETE ) continue;

			const bool is_delete = ( action == CONTENT_UPDATE_DELETE );

			const auto& data_rows = action_entry[ 1 ];
			if ( !data_rows.isArray() ) continue;

			switch ( content_type )
			{
				case CONTENT_TYPE_MAPPINGS:
				{
					for ( const auto& row : data_rows )
					{
						if ( !row.isArray() || row.size() < 2 ) continue;
						ContentUpdateMapping mapping;
						mapping.tag_id = row[ 0 ].asInt();
						const auto& hash_ids = row[ 1 ];
						if ( hash_ids.isArray() )
						{
							for ( const auto& hid : hash_ids )
								mapping.hash_ids.push_back( hid.asInt() );
						}
						if ( is_delete )
							result.mappings_delete.push_back( std::move( mapping ) );
						else
							result.mappings_add.push_back( std::move( mapping ) );
					}
					break;
				}
				case CONTENT_TYPE_TAG_PARENTS:
				{
					for ( const auto& row : data_rows )
					{
						if ( !row.isArray() || row.size() < 2 ) continue;
						const auto child_id = row[ 0 ].asInt();
						const auto parent_id = row[ 1 ].asInt();
						if ( is_delete )
							result.tag_parents_delete.emplace_back( child_id, parent_id );
						else
							result.tag_parents_add.emplace_back( child_id, parent_id );
					}
					break;
				}
				case CONTENT_TYPE_TAG_SIBLINGS:
				{
					for ( const auto& row : data_rows )
					{
						if ( !row.isArray() || row.size() < 2 ) continue;
						const auto bad_id = row[ 0 ].asInt();
						const auto good_id = row[ 1 ].asInt();
						if ( is_delete )
							result.tag_siblings_delete.emplace_back( bad_id, good_id );
						else
							result.tag_siblings_add.emplace_back( bad_id, good_id );
					}
					break;
				}
				default:
					break;
			}
		}
	}

	return result;
}

MetadataUpdate parseMetadataUpdate( const Json::Value& serialisable_info )
{
	MetadataUpdate result;

	if ( !serialisable_info.isArray() || serialisable_info.size() < 2 ) return result;

	const auto& updates_array = serialisable_info[ 0 ];
	result.next_update_due = serialisable_info[ 1 ].asInt64();

	if ( !updates_array.isArray() ) return result;

	for ( const auto& entry : updates_array )
	{
		if ( !entry.isArray() || entry.size() < 4 ) continue;

		MetadataUpdateEntry ue;
		ue.index = entry[ 0 ].asInt();
		const auto& hashes = entry[ 1 ];
		if ( hashes.isArray() )
		{
			for ( const auto& h : hashes )
				ue.hashes.push_back( h.asString() );
		}
		ue.begin = entry[ 2 ].asInt64();
		ue.end = entry[ 3 ].asInt64();

		result.updates.push_back( std::move( ue ) );
	}

	return result;
}

ParsedUpdate parseUpdateJson( const Json::Value& root )
{
	if ( !root.isArray() || root.size() < 3 )
		throw std::runtime_error( "Invalid update format: expected array of 3 elements" );

	const auto serialisable_type = root[ 0 ].asInt();
	const auto version = root[ 1 ].asInt();
	(void)version; // unused for now, could be used for version migration

	const auto& serialisable_info = root[ 2 ];

	switch ( serialisable_type )
	{
		case SERIALISABLE_TYPE_DEFINITIONS_UPDATE:
			return ParsedUpdate( parseDefinitionsUpdate( serialisable_info ) );
		case SERIALISABLE_TYPE_CONTENT_UPDATE:
			return ParsedUpdate( parseContentUpdate( serialisable_info ) );
		case SERIALISABLE_TYPE_METADATA:
			return ParsedUpdate( parseMetadataUpdate( serialisable_info ) );
		default:
			throw std::runtime_error(
				"Unknown serialisable type: " + std::to_string( serialisable_type ) );
	}
}

ParsedUpdate parseUpdateBytes( const std::vector< char >& compressed_data )
{
	const auto root = decompressToJson( compressed_data );
	return parseUpdateJson( root );
}

ParsedUpdate parseUpdateFile( const std::filesystem::path& path )
{
	const auto data = readFile( path );
	return parseUpdateBytes( data );
}

} // namespace idhan::hydrus::ptr
