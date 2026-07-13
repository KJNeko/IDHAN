//
// Created by kj16609 on 11/25/25.
//
#include "ArchiveGenerator.hpp"

#include <archive.h>
#include <archive_entry.h>
#include <iostream>

#include "MetadataModule.hpp"
#include "archives.hpp"
#include "crypto/simpleHasher.hpp"
#include "spdlog/spdlog.h"

std::vector< std::string_view > ArchiveGenerator::handleableMimes()
{
	return getHandleableMimesForArchives();
}

std::expected< std::vector< std::byte >, idhan::ModuleError > ArchiveGenerator::generate(
	idhan::ModuleCallData& data,
	std::array< std::byte, 256 / 8 > desired_hash )
{
	const auto& [ file_view, mime_name, extra ] = data;

	std::unique_ptr< archive, void ( * )( archive* ) > a {
		archive_read_new(), []( archive* ptr ) { archive_read_free( ptr ); }
	};

	archive_read_support_format_all( a.get() );
	archive_read_support_filter_all( a.get() );

	if ( const int r = archive_read_open_memory( a.get(), data.file_view.data(), data.file_view.size() ); r != 0 )
	{
		const char* err { archive_error_string( a.get() ) };
		return std::unexpected( idhan::ModuleError { err ? err : "archive_read_open_memory failed" } );
	}

	const auto target_hex { idhan::crypto::toHex( desired_hash ) };
	if ( !extra.isMember( target_hex ) )
		return std::unexpected( idhan::ModuleError { "Unable to generate file from desired hash" } );

	const std::string target_filename { extra[ target_hex ].asString() };

	archive_entry* entry { nullptr };

	idhan::MetadataInfo metadata {};
	idhan::MetadataInfoArchive archive_metadata {};

	auto ret { archive_read_next_header( a.get(), &entry ) };

	while ( ret == ARCHIVE_OK )
	{
		if ( archive_entry_filetype( entry ) != AE_IFREG ) // skip any non-files
		{
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
			spdlog::warn( "No file name for item in archive? Maybe encrypted but not flagged as such?" );
			ret = archive_read_next_header( a.get(), &entry );
			continue;
		}

		if ( !filename ) return std::unexpected( filename.error() );

		if ( *filename == target_filename )
		{
			// streams the entry, handling short reads and entries with no stored size
			return readArchiveEntryData( a.get() );
		}

		ret = archive_read_next_header( a.get(), &entry );
	}

	return std::unexpected(
		idhan::ModuleError {
			format_ns::format( "Failed to generate: No match found. Wanted to find \'{}\'", target_filename ) } );
}
