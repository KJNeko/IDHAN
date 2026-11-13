#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace idhan::downloader
{

[[nodiscard]] inline std::string normalizeResponseContentType( const std::string_view content_type )
{
	std::string type { content_type.substr( 0, content_type.find( ';' ) ) };
	std::ranges::transform(
		type, type.begin(), []( const unsigned char value ) { return static_cast< char >( std::tolower( value ) ); } );
	while ( !type.empty() && std::isspace( static_cast< unsigned char >( type.back() ) ) ) type.pop_back();
	while ( !type.empty() && std::isspace( static_cast< unsigned char >( type.front() ) ) ) type.erase( type.begin() );
	return type;
}

[[nodiscard]] inline bool looksLikeStructuredResponse( const std::string_view preview )
{
	// A server may label a login/error page as binary data. Inspect its opening bytes too.
	std::string_view opening { preview.substr( 0, 512 ) };
	if ( opening.starts_with( "\xEF\xBB\xBF" ) ) opening.remove_prefix( 3 );

	const auto first { opening.find_first_not_of( " \t\r\n" ) };
	if ( first == std::string_view::npos ) return false;

	const char initial { opening[ first ] };
	return initial == '<' || initial == '{' || initial == '[';
}

// The MIME type must already be normalized, with parameters removed.
[[nodiscard]] inline bool isDirectResponseContentType( const std::string_view type )
{
	if ( type.ends_with( "+xml" ) || type.ends_with( "+json" ) ) return false;

	if ( type.starts_with( "image/" ) ) return true;

	if ( type.starts_with( "video/" ) ) return true;

	if ( type.starts_with( "audio/" ) ) return true;

	static constexpr std::string_view application_types[] {
		"application/octet-stream",     "application/pdf",     "application/zip",  "application/x-7z-compressed",
		"application/x-rar-compressed", "application/vnd.rar", "application/gzip",
	};
	return std::ranges::contains( application_types, type );
}

[[nodiscard]] inline bool isDirectResponse( const std::string_view content_type, const std::string_view preview )
{
	if ( looksLikeStructuredResponse( preview ) ) return false;

	const auto type { normalizeResponseContentType( content_type ) };
	return isDirectResponseContentType( type );
}

} // namespace idhan::downloader
