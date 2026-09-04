#include "CookiePersistence.hpp"

#include <drogon/drogon.h>

#include <chrono>

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

	drogon::sync_wait(
		[ &cookies ]() -> drogon::Task< void >
		{
			const auto rows { co_await drogon::app().getDbClient()->execSqlCoro(
				"SELECT name, domain, path, value, secure, host_only, "
				"EXTRACT(EPOCH FROM expires)::BIGINT AS expires FROM downloader_cookies" ) };

			for ( const auto& row : rows )
				cookies.emplace_back(
					PersistedCookie {
						.name = row[ "name" ].as< std::string >(),
						.domain = row[ "domain" ].as< std::string >(),
						.path = row[ "path" ].as< std::string >(),
						.value = row[ "value" ].as< std::string >(),
						.secure = row[ "secure" ].as< bool >(),
						.host_only = row[ "host_only" ].as< bool >(),
						.expires = fromEpoch( row[ "expires" ].as< std::int64_t >() ) } );
		}() );

	log::debug( "Downloader loaded {} stored cookies", cookies.size() );
	return cookies;
}

void DatabaseCookies::upsert( const PersistedCookie& cookie )
{
	drogon::sync_wait(
		[ cookie ]() -> drogon::Task< void >
		{
			co_await drogon::app().getDbClient()->execSqlCoro(
				"INSERT INTO downloader_cookies (name, domain, path, value, secure, host_only, expires) "
				"VALUES ($1, $2, $3, $4, $5, $6, to_timestamp($7)) "
				"ON CONFLICT (name, domain, path) DO UPDATE SET value = EXCLUDED.value, "
				"secure = EXCLUDED.secure, host_only = EXCLUDED.host_only, expires = EXCLUDED.expires",
				cookie.name,
				cookie.domain,
				cookie.path,
				cookie.value,
				cookie.secure,
				cookie.host_only,
				toEpoch( cookie.expires ) );
		}() );
}

void DatabaseCookies::erase( const std::string_view name, const std::string_view domain, const std::string_view path )
{
	drogon::sync_wait(
		[ name = std::string { name }, domain = std::string { domain }, path = std::string { path } ]()
			-> drogon::Task< void >
		{
			co_await drogon::app().getDbClient()->execSqlCoro(
				"DELETE FROM downloader_cookies WHERE name = $1 AND domain = $2 AND path = $3", name, domain, path );
		}() );
}

void DatabaseCookies::pruneExpired()
{
	drogon::sync_wait(
		[]() -> drogon::Task< void >
		{
			co_await drogon::app().getDbClient()->execSqlCoro(
				"DELETE FROM downloader_cookies WHERE expires <= now()" );
		}() );
}

} // namespace idhan::downloader
