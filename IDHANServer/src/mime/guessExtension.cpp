#include "guessExtension.hpp"

#include <algorithm>
#include <cctype>

#include "Config.hpp"
#include "MimeIDs.hpp"
#include "logging/log.hpp"

namespace idhan::mime
{

static bool hasExtension( const std::string_view filename, const std::string_view extension )
{
	if ( filename.size() <= extension.size() ) return false;
	if ( filename[ filename.size() - extension.size() - 1 ] != '.' ) return false;

	return std::ranges::equal(
		filename.substr( filename.size() - extension.size() ),
		extension,
		[]( const char lhs, const char rhs ) { return std::tolower( static_cast< unsigned char >( lhs ) ) == rhs; } );
}

static bool cbzGuessEnabled()
{
	static const bool enabled { config::getSilentDefault< bool >( "parser.cbz", "guess_with_extension", true ) };

	return enabled;
}

MimeID guessMimeFromExtension( const MimeID mime_id, const std::string_view filename )
{
	if ( mime_id != mime_ids::APPLICATION_ZIP ) return mime_id;

	if ( !hasExtension( filename, "cbz" ) || !cbzGuessEnabled() ) return mime_id;

	log::debug( "Treating '{}' as a comic book zip because of its extension", filename );

	return mime_ids::COMICBOOK_ZIP;
}

} // namespace idhan::mime
