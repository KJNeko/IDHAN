#include "ArchiveMimeParser.hpp"

#include <archive.h>
#include <archive_entry.h>
#include <memory>

#include "MimeIDs.hpp"
#include "archives.hpp"
#include "spdlog/spdlog.h"

//! The manifest a Pixiv Ugoira carries, and the only thing that distinguishes one from a plain zip.
constexpr std::string_view UGOIRA_MANIFEST { "animation.json" };

//! Whether \p path names the Ugoira manifest, at the archive root or inside a directory.
static bool isUgoiraManifest( const char* path )
{
	if ( path == nullptr ) return false;

	const std::string_view name { path };
	const auto separator { name.find_last_of( "/\\" ) };

	return ( separator == std::string_view::npos ? name : name.substr( separator + 1 ) ) == UGOIRA_MANIFEST;
}

std::string_view ArchiveMimeParser::name()
{
	return "Archive mime parser";
}

idhan::ModuleVersion ArchiveMimeParser::version()
{
	return { .m_major = 1, .m_minor = 0, .m_patch = 0 };
}

std::vector< idhan::MimeID > ArchiveMimeParser::handleableMimes()
{
	return { idhan::mime_ids::APPLICATION_ZIP };
}

std::expected< idhan::MimeID, idhan::ModuleError > ArchiveMimeParser::parseMime( idhan::ModuleCallData& data )
{
	ArchiveModuleReader reader { data.file };

	std::unique_ptr< archive, void ( * )( archive* ) > a {
		archive_read_new(), []( archive* ptr ) { archive_read_free( ptr ); }
	};

	archive_read_support_format_all( a.get() );
	archive_read_support_filter_all( a.get() );
	archive_read_set_option( a.get(), "zip", "compact-utf8", "on" );

	if ( const auto opened { reader.open( a.get() ) }; !opened ) return std::unexpected( opened.error() );

	archive_entry* entry { nullptr };

	auto ret { archive_read_next_header( a.get(), &entry ) };

	while ( ret == ARCHIVE_OK || ret == ARCHIVE_WARN )
	{
		if ( archive_entry_filetype( entry ) == AE_IFREG )
		{
			// the manifest name is ASCII, so the raw pathname answers this without a transcode
			const char* path { archive_entry_pathname_utf8( entry ) };
			if ( path == nullptr ) path = archive_entry_pathname( entry );

			if ( isUgoiraManifest( path ) )
			{
				spdlog::debug( "Archive carries {}, refining it to a Pixiv Ugoira", UGOIRA_MANIFEST );
				return idhan::mime_ids::PIXIV_UGOIRA;
			}
		}

		ret = archive_read_next_header( a.get(), &entry );
	}

	if ( ret != ARCHIVE_EOF )
	{
		const char* err { archive_error_string( a.get() ) };
		return std::unexpected( idhan::ModuleError { err ? err : "archive_read_next_header failed" } );
	}

	return idhan::mime_ids::APPLICATION_ZIP;
}
