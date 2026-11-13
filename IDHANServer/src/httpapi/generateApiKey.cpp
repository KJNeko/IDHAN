#include "generateApiKey.hpp"

#include <span>

#include "idhan/SHA256.hpp"
#include "idhan/db/DBContext.hpp"
#include "idhan/http/routing/ReflectedRoutes.hpp"

namespace idhan::httpapi
{

static constexpr const char* insert_if_empty_sql {
	//language=postgresql
	R"SQL(
		INSERT INTO auth_keys (key_hash)
		SELECT
		    gen_random_bytes(32)
		WHERE NOT EXISTS (
		        SELECT
		            1
		        FROM auth_keys
		)
		RETURNING key_hash
	)SQL"
};

http::HTTPTask< http::JsonResult< GeneratedApiKeyResponse > > generateApiKey(
	db::DBContext& db,
	[[maybe_unused]] http::RequestContext context )
{
	auto rows { co_await db.query( insert_if_empty_sql ).rows< std::span< const std::byte > >() };

	if ( rows.empty() )
		co_return http::Response::error( 404, "An API key already exists. Use the existing key to manage keys." );

	const auto key_bytes { std::get< 0 >( rows.row( 0 ) ) };
	const SHA256 key { SHA256::fromBuffer( key_bytes ) };

	co_return http::JsonResponse< GeneratedApiKeyResponse > { { key.hex() } };
}

} // namespace idhan::httpapi
