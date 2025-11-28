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

	if ( const int r = archive_read_open_memory( a.get(), data.file_view.data(), data.file_view.size() ); r != 0 )
	{
		return std::unexpected( idhan::ModuleError { archive_error_string( a.get() ) } );
	}

	idhan::MetadataInfo metadata {};
	idhan::MetadataInfoArchive archive_metadata {};

	archive_entry* entry { nullptr };

	Json::Value json {};

	auto ret { archive_read_next_header( a.get(), &entry ) };

	do
	{
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

		const char* filename_raw { archive_entry_pathname( entry ) };

		if ( filename_raw == nullptr )
		{
			if ( auto filename_utf8 = archive_entry_pathname( entry ); filename_utf8 )
			{
				filename_raw = filename_utf8;
			}
			else
			{
				spdlog::warn( "No file name for item in archive?" );
				ret = archive_read_next_header( a.get(), &entry );
				continue;
			}
		}

		const auto filename { sanitizeEncoding( filename_raw ) };
		if ( !filename ) return std::unexpected( filename.error() );

		spdlog::trace( "Cleaned path to {}", *filename );

		const auto file_size { static_cast< std::size_t >( archive_entry_size( entry ) ) };

		std::vector< std::byte > file_data {};
		file_data.resize( file_size );

		if ( archive_read_data( a.get(), file_data.data(), file_size ) < 0 )
		{
			spdlog::error( "Unable to read archive data" );
			return std::unexpected( idhan::ModuleError { archive_error_string( a.get() ) } );
		}

		const auto file_hash { idhan::crypto::hashData( file_data.data(), file_data.size() ) };

		archive_metadata.contained_hashes.emplace_back( file_hash );
		archive_metadata.m_size += file_size;
		json[ idhan::crypto::toHex( file_hash ) ] = *filename;
		spdlog::trace( "Got hash {}", idhan::crypto::toHex( file_hash ) );

		ret = archive_read_next_header( a.get(), &entry );
	}
	while ( ret == ARCHIVE_OK );

	json[ "encrypted" ] = archive_metadata.encrypted;

	spdlog::trace( "Finished processing" );

	if ( ret != ARCHIVE_EOF )
	{
		return std::unexpected( idhan::ModuleError { archive_error_string( a.get() ) } );
	}

	metadata.m_simple_type = idhan::SimpleMimeType::ARCHIVE;
	metadata.m_metadata = archive_metadata;
	metadata.m_extra = json;

	return metadata;
}
