#include "authKeys.hpp"

namespace idhan::auth
{

drogon::Task< bool > keyExists( const SHA256& key_hash, DbClientPtr db )
{
	const auto rows {
		co_await db->execSqlCoro( "SELECT 1 FROM auth_keys WHERE key_hash = $1 LIMIT 1", key_hash.toVec() )
	};

	co_return !rows.empty();
}

drogon::Task< std::size_t > keyCount( DbClientPtr db )
{
	const auto rows { co_await db->execSqlCoro( "SELECT count(*) FROM auth_keys" ) };

	if ( rows.empty() ) co_return 0;

	co_return rows[ 0 ][ 0 ].as< std::size_t >();
}

} // namespace idhan::auth
