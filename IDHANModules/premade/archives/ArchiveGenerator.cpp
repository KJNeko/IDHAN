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

std::expected< void, idhan::ModuleError > ArchiveGenerator::generate(
	idhan::ModuleCallData& data,
	std::array< std::byte, 256 / 8 > desired_hash,
	idhan::ModuleSink& out )
{
	const auto& extra { data.extra };

	// Declared before the handle so it outlives it: libarchive holds a pointer into the reader's
	// chunk, and reverse-order destruction is what keeps that pointer valid for the handle's life.
	ArchiveModuleReader reader { data.file };

	std::unique_ptr< archive, void ( * )( archive* ) > a {
		archive_read_new(), []( archive* ptr ) { archive_read_free( ptr ); }
	};

	archive_read_support_format_all( a.get() );
	archive_read_support_filter_all( a.get() );

	if ( const auto opened { reader.open( a.get() ) }; !opened ) return std::unexpected( opened.error() );

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

		if ( entryFilename( entry, filename ) == EntryNameResult::UNNAMED )
		{
			ret = archive_read_next_header( a.get(), &entry );
			continue;
		}

		if ( !filename ) return std::unexpected( filename.error() );

		if ( *filename == target_filename )
		{
			// Sized from the header where the format records it -- zip does -- so the sink allocates
			// once instead of growing as the entry decompresses into it.
			if ( archive_entry_size_is_set( entry ) )
			{
				const auto declared { archive_entry_size( entry ) };
				if ( declared > 0 )
				{
					if ( const auto reserved { out.reserve( static_cast< std::size_t >( declared ) ) }; !reserved )
						return std::unexpected( reserved.error() );
				}
			}

			// streams the entry, handling short reads and entries with no stored size
			return writeArchiveEntryData( a.get(), out );
		}

		ret = archive_read_next_header( a.get(), &entry );
	}

	return std::unexpected(
		idhan::ModuleError {
			format_ns::format( "Failed to generate: No match found. Wanted to find \'{}\'", target_filename ) } );
}
