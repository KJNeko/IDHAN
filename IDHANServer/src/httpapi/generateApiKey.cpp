#include "generateApiKey.hpp"

#include <folly/io/IOBuf.h>

#include <simdjson.h>
#include <span>
#include <stdexcept>
#include <vector>

#include "crypto/SHA256.hpp"
#include "idhan/db/DBContext.hpp"

namespace idhan::httpapi
{

static constexpr const char* insert_if_empty_sql {
	"INSERT INTO auth_keys (key_hash) SELECT gen_random_bytes(32) "
	"WHERE NOT EXISTS (SELECT 1 FROM auth_keys) RETURNING key_hash"
};

http::RouteDefinition generateApiKeyRoute( db::DBContext& db )
{
	auto handler = [ &db ]( [[maybe_unused]] http::RequestContext context ) -> http::HTTPTask< http::Response >
	{
		auto rows { co_await db.query( insert_if_empty_sql ).rows< std::span< const std::byte > >() };

		if ( rows.empty() )
			co_return http::Response::error( 404, "An API key already exists. Use the existing key to manage keys." );

		const auto key_bytes { std::get< 0 >( rows.row( 0 ) ) };
		const SHA256 key { SHA256::fromBuffer( std::vector< std::byte > { key_bytes.begin(), key_bytes.end() } ) };

		simdjson::builder::string_builder builder;
		builder.start_object();
		builder.append_key_value( "key", key.hex() );
		builder.end_object();

		auto json { builder.view() };
		if ( json.error() ) throw std::runtime_error( simdjson::error_message( json.error() ) );

		co_return http::Response::json( 200, folly::IOBuf::copyBuffer( *json ) );
	};

	return http::RouteDefinition {
		http::RouteMetadata { proxygen::HTTPMethod::GET, "/generate_api_key", {}, {} }, std::move( handler )
	};
}

} // namespace idhan::httpapi
