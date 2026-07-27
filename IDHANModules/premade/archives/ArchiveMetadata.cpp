//
// Created by kj16609 on 11/24/25.
//

#include "ArchiveMetadata.hpp"

#include <json/value.h>

#include <archive.h>
#include <archive_entry.h>
#include <iostream>
#include <memory>

#include "archives.hpp"
#include "crypto/simpleHasher.hpp"
#include "spdlog/spdlog.h"

std::string_view ArchiveMetadata::name()
{
	return "Archive metadata module";
}

idhan::ModuleVersion ArchiveMetadata::version()
{
	return { .m_major = 1, .m_minor = 0, .m_patch = 0 };
}

std::vector< std::string_view > ArchiveMetadata::handleableMimes()
{
	return getHandleableMimesForArchives();
}

std::expected< idhan::MetadataInfo, idhan::ModuleError > ArchiveMetadata::parseFile( idhan::ModuleCallData& data )
{
	std::unique_ptr< archive, void ( * )( archive* ) > a {
		archive_read_new(), []( archive* ptr ) { archive_read_free( ptr ); }
	};

	archive_read_support_format_all( a.get() );
	archive_read_support_filter_all( a.get() );
	archive_read_set_option( a.get(), "zip", "compact-utf8", "on" );

	if ( const int r = archive_read_open_memory( a.get(), data.file_view.data(), data.file_view.size() ); r != 0 )
	{
		const char* err { archive_error_string( a.get() ) };
		return std::unexpected( idhan::ModuleError { err ? err : "archive_read_open_memory failed" } );
	}

	idhan::MetadataInfo metadata {};
	idhan::MetadataInfoArchive archive_metadata {};

	archive_entry* entry { nullptr };

	Json::Value json {};

	auto ret { archive_read_next_header( a.get(), &entry ) };

	while ( ret == ARCHIVE_OK || ret == ARCHIVE_WARN )
	{
		// ARCHIVE_WARN is recoverable: the entry is still usable, so log and carry on rather than
		// aborting the whole archive (the post-loop ret != ARCHIVE_EOF check would otherwise treat
		// the warning as fatal).
		if ( ret == ARCHIVE_WARN )
		{
			const char* warn { archive_error_string( a.get() ) };
			spdlog::warn( "Archive warning while reading entry: {}", warn ? warn : "unknown" );
		}

		if ( archive_entry_filetype( entry ) != AE_IFREG ) // skip anything that isn't a file
		{
			ret = archive_read_next_header( a.get(), &entry );
			continue;
		}

		if ( archive_entry_is_encrypted( entry ) )
		{
			spdlog::debug( "Archive has an encrypted item" );
			archive_metadata.encrypted = true;
			ret = archive_read_next_header( a.get(), &entry );
			continue;
		}

		std::expected< std::string, idhan::ModuleError > filename {};

		if ( const char* filename_raw = archive_entry_pathname( entry ); filename_raw != nullptr )
		{
			filename = sanitizeEncoding( filename_raw );
		}
		else if ( const char* utf8_filename_raw = archive_entry_pathname_utf8( entry ); utf8_filename_raw != nullptr )
		{
			filename = sanitizeEncoding( utf8_filename_raw );
		}
		else if ( const wchar_t* w_filename_raw = archive_entry_pathname_w( entry ); w_filename_raw != nullptr )
		{
			// Wide-only name: convert it rather than dropping the entry.
			filename = wideToUtf8( w_filename_raw );
		}
		else
		{
			spdlog::warn( "No file name for item in archive? Maybe encrypted?" );
			ret = archive_read_next_header( a.get(), &entry );
			continue;
		}

		if ( !filename ) return std::unexpected( filename.error() );

		spdlog::trace( "Cleaned path to {}", *filename );

		// Only the digest and size are needed here, so stream the entry through an incremental hash
		// rather than buffering the whole (decompressed) entry -- a large member or decompression bomb
		// would otherwise inflate memory to the entry's uncompressed size.
		const auto entry_hash_e { hashArchiveEntryData( a.get() ) };
		if ( !entry_hash_e )
		{
			spdlog::error( "Unable to read archive data" );
			return std::unexpected( entry_hash_e.error() );
		}
		const auto& [ file_hash, file_size ] { *entry_hash_e };

		archive_metadata.contained_hashes.emplace_back( file_hash );
		archive_metadata.m_size += file_size;
		json[ idhan::crypto::toHex( file_hash ) ] = *filename;
		spdlog::trace( "Got hash {}", idhan::crypto::toHex( file_hash ) );

		ret = archive_read_next_header( a.get(), &entry );
	}

	json[ "encrypted" ] = archive_metadata.encrypted;

	spdlog::trace( "Finished processing" );

	if ( ret != ARCHIVE_EOF )
	{
		const char* err { archive_error_string( a.get() ) };
		return std::unexpected( idhan::ModuleError { err ? err : "archive_read_next_header failed" } );
	}

	metadata.m_simple_type = idhan::SimpleMimeType::ARCHIVE;
	metadata.m_metadata = archive_metadata;
	metadata.m_extra = json;

	return metadata;
}
