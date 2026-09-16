#include "CookiePersistence.hpp"

#include <folly/coro/BlockingWait.h>

#include <chrono>

#include "DownloadSessionManager.hpp"
#include "idhan/db/DBContext.hpp"
#include "logging/log.hpp"

namespace idhan::downloader
{

static std::chrono::system_clock::time_point fromEpoch( const std::int64_t seconds )
{
	return std::chrono::system_clock::time_point { std::chrono::seconds { seconds } };
}

static std::int64_t toEpoch( const std::chrono::system_clock::time_point value )
{
	return std::chrono::duration_cast< std::chrono::seconds >( value.time_since_epoch() ).count();
}

std::vector< PersistedCookie > DatabaseCookies::loadAll()
{
	std::vector< PersistedCookie > cookies {};

	auto rows { folly::coro::blockingWait(
		DownloadSessionManager::dbContext()
			.query(
				"SELECT name, domain, path, value, secure, host_only, "
				"EXTRACT(EPOCH FROM expires)::BIGINT AS expires FROM downloader_cookies" )
			.rows< std::string, std::string, std::string, std::string, bool, bool, std::int64_t >() ) };

	for ( auto [ name, domain, path, value, secure, host_only, expires ] : rows )
		cookies.emplace_back(
			PersistedCookie {
				.name = std::move( name ),
				.domain = std::move( domain ),
				.path = std::move( path ),
				.value = std::move( value ),
				.secure = secure,
				.host_only = host_only,
				.expires = fromEpoch( expires ) } );

	log::debug( "Downloader loaded {} stored cookies", cookies.size() );
	return cookies;
}

void DatabaseCookies::upsert( const PersistedCookie& cookie )
{
	db::DBBindings bindings {};
	bindings.bind( cookie.name );
	bindings.bind( cookie.domain );
	bindings.bind( cookie.path );
	bindings.bind( cookie.value );
	bindings.bind( cookie.secure );
	bindings.bind( cookie.host_only );
	bindings.bind( toEpoch( cookie.expires ) );

	folly::coro::blockingWait(
		DownloadSessionManager::dbContext().query(
			"INSERT INTO downloader_cookies (name, domain, path, value, secure, host_only, expires) "
			"VALUES ($1, $2, $3, $4, $5, $6, to_timestamp($7)) "
			"ON CONFLICT (name, domain, path) DO UPDATE SET value = EXCLUDED.value, "
			"secure = EXCLUDED.secure, host_only = EXCLUDED.host_only, expires = EXCLUDED.expires",
			std::move( bindings ) ) );
}

void DatabaseCookies::erase( const std::string_view name, const std::string_view domain, const std::string_view path )
{
	db::DBBindings bindings {};
	bindings.bind( std::string { name } );
	bindings.bind( std::string { domain } );
	bindings.bind( std::string { path } );

	folly::coro::blockingWait(
		DownloadSessionManager::dbContext().query(
			"DELETE FROM downloader_cookies WHERE name = $1 AND domain = $2 AND path = $3", std::move( bindings ) ) );
}

void DatabaseCookies::pruneExpired()
{
	folly::coro::blockingWait(
		DownloadSessionManager::dbContext().query( "DELETE FROM downloader_cookies WHERE expires <= now()" ) );
}

} // namespace idhan::downloader
