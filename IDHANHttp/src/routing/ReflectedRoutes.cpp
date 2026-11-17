#include "idhan/http/routing/ReflectedRoutes.hpp"

#include <folly/FileUtil.h>

namespace idhan::http
{

std::expected< std::string, ParseError > readReflectedBody( RequestContext& context )
{
	auto body { std::move( context ).takeBody() };
	const auto* buffer { std::get_if< std::unique_ptr< folly::IOBuf > >( &body ) };
	if ( buffer ) return ( *buffer )->to< std::string >();

	auto& upload { std::get< TemporaryUpload >( body ) };
	std::string text;
	if ( upload.size() > text.max_size() )
		return std::unexpected( ParseError { "body", "Request body exceeds addressable string size", 500 } );

	text.resize( upload.size() );
	const auto file { upload.duplicateFile() };
	const auto read { folly::preadFull( file.fd(), text.data(), text.size(), 0 ) };
	if ( read < 0 ) return std::unexpected( ParseError { "body", "Unable to read request upload", 500 } );

	if ( static_cast< std::size_t >( read ) != text.size() )
		return std::unexpected( ParseError { "body", "Incomplete request upload", 500 } );

	return text;
}

std::vector< PathParameter > reflectedPathParameters( const std::string_view path )
{
	std::vector< PathParameter > parameters;
	std::size_t offset {};
	while ( ( offset = path.find( '{', offset ) ) != std::string_view::npos )
	{
		const auto end { path.find( '}', offset ) };
		if ( end == std::string_view::npos ) throw std::invalid_argument( "Unclosed route capture" );

		auto name { path.substr( offset + 1, end - offset - 1 ) };
		if ( name.starts_with( '*' ) ) name.remove_prefix( 1 );

		parameters.push_back(
			{ std::string { name },
			  []( std::string_view ) noexcept -> std::expected< void, ParseError > { return {}; } } );
		offset = end + 1;
	}
	return parameters;
}

} // namespace idhan::http
