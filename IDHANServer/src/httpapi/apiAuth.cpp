#include "apiAuth.hpp"

#include <folly/io/IOBuf.h>
#include <proxygen/lib/http/HTTPMessage.h>

#include <array>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>

#include "auth/authKeys.hpp"
#include "idhan/SHA256.hpp"
#include "idhan/db/DBContext.hpp"
#include "logging/log.hpp"

namespace idhan::httpapi
{

constexpr std::array< std::string_view, 3 > key_headers { { "Authorization", "X-API-Key", "IDHAN-API-Key" } };

[[nodiscard]] static std::string headerKey( const proxygen::HTTPMessage& message )
{
	for ( const auto header : key_headers )
	{
		auto value { message.getHeaders().getSingleOrEmpty( std::string { header } ) };
		if ( !value.empty() ) return value;
	}

	return {};
}

http::RouteDefinition verifyRoute( db::DBContext& db )
{
	return http::RouteDefinition {
		http::RouteMetadata { proxygen::HTTPMethod::GET, "/auth/verify", {}, { apiAuthValidation( db ) } },
		[]( http::RequestContext ) -> http::HTTPTask< http::Response >
		{ co_return http::Response::json( 200, folly::IOBuf::copyBuffer( R"({"authenticated":true})" ) ); }
	};
}

http::HTTPTask< std::optional< KeyRejection > > verifyApiKey( const std::string_view key, db::DBContext& db )
{
	if ( key.size() != 64 )
		co_return KeyRejection { 400, "Invalid key length for idhan_key. Expected 64 characters in hex" };

	const auto hash { SHA256::fromHex( key ) };
	if ( !hash ) co_return KeyRejection { 400, "idhan_key must be hexadecimal" };

	if ( !co_await auth::keyExists( *hash, db ) )
	{
		log::warn( "Invalid API key given!" );
		co_return KeyRejection { 401, "Invalid API Key" };
	}

	co_return std::nullopt;
}

http::RequestValidation apiAuthValidation( db::DBContext& db )
{
	return [ &db ]( const proxygen::HTTPMessage& message, const http::RequestParameters& parameters )
	           -> http::HTTPTask< std::optional< http::Response > >
	{
#ifdef IDHAN_DISABLE_API_AUTH
		static std::once_flag auth_disabled_flag;
		std::call_once( auth_disabled_flag, [] { log::warn( "!!! API Auth Disabled. Approving all requests !!!" ); } );
		co_return std::nullopt;
#else
		const auto param_e { parameters.optional< std::string >( "idhan_key" ) };
		if ( !param_e ) co_return http::Response::error( 400, param_e.error().message );

		const auto key { ( *param_e && !( *param_e )->empty() ) ? **param_e : headerKey( message ) };

		if ( key.empty() )
			co_return http::Response::error( 400, "idhan_key parameter not provided in parameters or header" );

		const auto rejection { co_await verifyApiKey( key, db ) };
		if ( rejection ) co_return http::Response::error( rejection->status, rejection->message );

		co_return std::nullopt;
#endif
	};
}

} // namespace idhan::httpapi
