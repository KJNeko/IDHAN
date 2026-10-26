#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace idhan::downloader
{

// Conservative evidence of a single file: page/document APIs and unknown MIME types stay unclassified.
[[nodiscard]] inline bool isDirectResponse( std::string_view content_type, std::string_view preview )
{
	std::string type { content_type.substr( 0, content_type.find( ';' ) ) };
	std::ranges::transform(
		type, type.begin(), []( const unsigned char value ) { return static_cast< char >( std::tolower( value ) ); } );
	while ( !type.empty() && std::isspace( static_cast< unsigned char >( type.back() ) ) ) type.pop_back();
	while ( !type.empty() && std::isspace( static_cast< unsigned char >( type.front() ) ) ) type.erase( type.begin() );

	// A server may label a login/error page as binary data. Inspect its opening bytes too.
	std::string_view opening { preview.substr( 0, 512 ) };
	if ( opening.starts_with( "\xEF\xBB\xBF" ) ) opening.remove_prefix( 3 );
	const auto first { opening.find_first_not_of( " \t\r\n" ) };
	if ( first != std::string_view::npos )
	{
		const char initial { opening[ first ] };
		if ( initial == '<' || initial == '{' || initial == '[' ) return false;
	}

	if ( type.ends_with( "+xml" ) || type.ends_with( "+json" ) ) return false;

	return type.starts_with( "image/" ) || type.starts_with( "video/" ) || type.starts_with( "audio/" )
	    || type == "application/octet-stream" || type == "application/pdf" || type == "application/zip"
	    || type == "application/x-7z-compressed" || type == "application/x-rar-compressed"
	    || type == "application/vnd.rar" || type == "application/gzip";
}

} // namespace idhan::downloader
