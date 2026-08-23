#include "ArchiveMimeParser.hpp"

#include <algorithm>
#include <archive.h>
#include <archive_entry.h>
#include <charconv>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include "MimeIDs.hpp"
#include "archives.hpp"
#include "imageExtensions.hpp"
#include "spdlog/spdlog.h"

constexpr std::string_view UGOIRA_MANIFEST { "animation.json" };

constexpr std::size_t MINIMUM_FRAMES { 2 };

//! Whether \p path names the Ugoira manifest, at the archive root or inside a directory.
static bool isUgoiraManifest( const std::string_view path )
{
	const auto separator { path.find_last_of( "/\\" ) };

	return ( separator == std::string_view::npos ? path : path.substr( separator + 1 ) ) == UGOIRA_MANIFEST;
}

class UgoiraFrameTest
{
	std::vector< std::uint64_t > m_numbers {};
	std::size_t m_digits { 0 }; //!< Digit count the first frame name used; the rest must match it.
	bool m_manifest { false };
	bool m_failed { false };

  public:

	void offer( std::string_view path, bool is_regular );

	void offer( archive_entry* entry );

	[[nodiscard]] bool passes();
};

void UgoiraFrameTest::offer( std::string_view path, const bool is_regular )
{
	if ( m_manifest ) return;

	if ( path.starts_with( "./" ) ) path.remove_prefix( 2 );

	// a manifest settles it wherever it turns up, including after a member that broke another rule
	if ( is_regular && isUgoiraManifest( path ) )
	{
		m_manifest = true;
		return;
	}

	if ( m_failed ) return;

	if ( path.empty() ) return; // some writers spell the archive root as an entry of its own

	if ( path.find_first_of( "/\\" ) != std::string_view::npos || !is_regular )
	{
		m_failed = true;
		return;
	}

	const auto dot { path.find_last_of( '.' ) };

	if ( dot == std::string_view::npos || !idhan::premade::isFrameExtension( path.substr( dot + 1 ) ) )
	{
		m_failed = true;
		return;
	}

	const auto stem { path.substr( 0, dot ) };

	if ( stem.empty() || stem.size() != 6 )
	{
		m_failed = true;
		return;
	}

	std::uint64_t number { 0 };

	const bool all_digits { stem.find_first_not_of( "0123456789" ) == std::string_view::npos };
	const bool parsed { std::from_chars( stem.data(), stem.data() + stem.size(), number ).ec == std::errc {} };

	if ( !all_digits || !parsed )
	{
		m_failed = true;
		return;
	}

	m_digits = stem.size();
	m_numbers.push_back( number );
}

void UgoiraFrameTest::offer( archive_entry* entry )
{
	const char* path { archive_entry_pathname_utf8( entry ) };
	if ( path == nullptr ) path = archive_entry_pathname( entry );

	if ( path == nullptr )
	{
		m_failed = true;
		return;
	}

	offer( path, archive_entry_filetype( entry ) == AE_IFREG );
}

bool UgoiraFrameTest::passes()
{
	if ( m_manifest ) return true;

	if ( m_failed || m_numbers.size() < MINIMUM_FRAMES ) return false;

	std::ranges::sort( m_numbers );

	// counting up from zero with nothing skipped puts the nth smallest number at n
	for ( std::size_t i = 0; i < m_numbers.size(); ++i )
		if ( m_numbers[ i ] != static_cast< std::uint64_t >( i ) ) return false;

	return true;
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
	UgoiraFrameTest frame_test {};

	auto ret { archive_read_next_header( a.get(), &entry ) };

	while ( ret == ARCHIVE_OK || ret == ARCHIVE_WARN )
	{
		frame_test.offer( entry );

		if ( archive_entry_filetype( entry ) == AE_IFREG )
		{
			// the manifest name is ASCII, so the raw pathname answers this without a transcode
			const char* path { archive_entry_pathname_utf8( entry ) };
			if ( path == nullptr ) path = archive_entry_pathname( entry );

			if ( path != nullptr && isUgoiraManifest( path ) )
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

	if ( frame_test.passes() )
	{
		spdlog::debug( "Archive is a numbered image sequence at its root, refining it to a Pixiv Ugoira" );
		return idhan::mime_ids::PIXIV_UGOIRA;
	}

	return idhan::mime_ids::APPLICATION_ZIP;
}
