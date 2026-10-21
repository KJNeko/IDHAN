#include "authKeys.hpp"

#include "idhan/db/DBContext.hpp"

namespace idhan::auth
{

runtime::Task< bool > keyExists( const SHA256& key_hash, db::DBContext& db )
{
	const auto raw { key_hash.data() };

	db::DBBindings bindings {};
	bindings.bind( std::vector< std::byte > { raw.begin(), raw.end() } );

	const auto rows { co_await db.query( "SELECT 1 FROM auth_keys WHERE key_hash = $1 LIMIT 1", std::move( bindings ) )
		                  .rows< std::int32_t >() };

	co_return !rows.empty();
}

runtime::Task< std::size_t > keyCount( db::DBContext& db )
{
	const auto rows { co_await db.query( "SELECT count(*) FROM auth_keys" ).rows< std::int64_t >() };

	if ( rows.empty() ) co_return 0;

	co_return static_cast< std::size_t >( std::get< 0 >( rows.row( 0 ) ) );
}

} // namespace idhan::auth
