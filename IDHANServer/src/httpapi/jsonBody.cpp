#include "jsonBody.hpp"

#include <folly/FileUtil.h>
#include <folly/io/IOBuf.h>

#include <cerrno>
#include <system_error>

#include "idhan/http/ParameterRules.hpp"

namespace idhan::httpapi
{

std::expected< std::string, http::Response > readRequestBody( http::RequestContext& context )
{
	auto body { std::move( context ).takeBody() };
	std::string text {};
	auto* const buffer { std::get_if< std::unique_ptr< folly::IOBuf > >( &body ) };
	if ( buffer )
		text = ( *buffer )->to< std::string >();
	else
	{
		auto& upload { std::get< http::TemporaryUpload >( body ) };
		if ( upload.size() > text.max_size() )
			return std::unexpected( http::Response::error( 500, "Request body exceeds addressable string size" ) );

		text.resize( upload.size() );
		const auto file { upload.duplicateFile() };
		const auto read { folly::preadFull( file.fd(), text.data(), text.size(), 0 ) };
		if ( read < 0 )
			return std::unexpected(
				http::Response::error(
					500, std::system_error( errno, std::generic_category(), "Reading request upload" ).what() ) );

		if ( static_cast< std::size_t >( read ) != text.size() )
			return std::unexpected( http::Response::error( 500, "Incomplete request upload" ) );
	}

	return text;
}

std::expected< std::string, http::Response > readValidatedJsonBody( http::RequestContext& context )
{
	auto text { readRequestBody( context ) };
	if ( !text ) return std::unexpected( std::move( text.error() ) );

	simdjson::dom::parser parser;
	auto document { parser.parse( *text ) };
	if ( document.error() )
		return std::unexpected( http::Response::error( 400, simdjson::error_message( document.error() ) ) );

	const auto* rules { context.parameterRules() };
	if ( rules )
	{
		const auto unknown { http::unknownJsonParameters( *document, rules->json ) };
		const auto method { context.message().getMethod() };
		const auto path { context.message().getPathAsStringPiece() };
		auto rejection { http::handleUnknownParameters(
			unknown, "JSON", *method, std::string_view { path.data(), path.size() }, rules->unknown ) };
		if ( rejection ) return std::unexpected( http::Response::error( 400, std::move( *rejection ) ) );
	}
	return std::move( *text );
}

} // namespace idhan::httpapi
