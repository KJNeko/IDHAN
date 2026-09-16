#include "jsonBody.hpp"

#include <folly/io/IOBuf.h>

#include <format>

#include "idhan/http/HTTPConfig.hpp"
#include "idhan/http/ValidatedJson.hpp"

namespace idhan::httpapi
{

std::expected< std::string, http::Response > readValidatedJsonBody( http::RequestContext& context )
{
	auto body { std::move( context ).takeBody() };
	if ( !std::holds_alternative< std::unique_ptr< folly::IOBuf > >( body ) )
		return std::unexpected(
			http::Response::error(
				413,
				std::format(
					"JSON request bodies are limited to {} bytes", idhan::http::json_body_limit ) ) );

	const auto text { std::get< std::unique_ptr< folly::IOBuf > >( body )->to< std::string >() };

	auto validated { http::ValidatedJson::parse( text ) };
	if ( !validated ) return std::unexpected( http::Response::error( 400, validated.error().message ) );

	return std::string { validated->text() };
}

} // namespace idhan::httpapi
