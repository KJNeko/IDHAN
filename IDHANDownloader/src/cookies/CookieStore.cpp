#include "cookies/CookieStore.hpp"

#include <ada.h>
#include <algorithm>
#include <ranges>

#include "URLUtils.hpp"
#include "http/HttpMessage.hpp"

namespace idhan::downloader
{

bool cookieDomainMatches( const std::string_view host, const std::string_view domain, const bool host_only )
{
	if ( host == domain ) return true;
	if ( host_only ) return false;

	return detail::isSubdomainOf( host, domain );
}

bool cookiePathMatches( const std::string_view request_path, const std::string_view cookie_path )
{
	if ( cookie_path.empty() || cookie_path == "/" ) return true;
	if ( !request_path.starts_with( cookie_path ) ) return false;
	if ( request_path.size() == cookie_path.size() ) return true;

	return cookie_path.back() == '/' || request_path[ cookie_path.size() ] == '/';
}

std::string defaultCookiePath( const std::string_view request_path )
{
	const auto slash { request_path.find_last_of( '/' ) };

	if ( request_path.empty() || request_path.front() != '/' || slash == 0 ) return "/";

	return std::string { request_path.substr( 0, slash ) };
}

static bool expired( const Cookie& cookie, const std::chrono::system_clock::time_point now )
{
	return cookie.expires.has_value() && *cookie.expires <= now;
}

static bool matches(
	const Cookie& cookie,
	const std::string_view host,
	const std::string_view path,
	const bool secure,
	const std::chrono::system_clock::time_point now )
{
	if ( expired( cookie, now ) ) return false;
	if ( cookie.secure && !secure ) return false;
	if ( !cookieDomainMatches( host, cookie.domain, cookie.host_only ) ) return false;

	return cookiePathMatches( path, cookie.path );
}

static void upsert( std::vector< Cookie >& cookies, Cookie cookie )
{
	const auto found {
		std::ranges::find_if( cookies, [ & ]( const Cookie& held ) { return held.sameIdentity( cookie ); } )
	};

	if ( found == cookies.end() )
		cookies.emplace_back( std::move( cookie ) );
	else
		*found = std::move( cookie );
}

static void remove(
	std::vector< Cookie >& cookies,
	const std::string_view name,
	const std::string_view domain,
	const std::string_view path )
{
	const auto removed { std::ranges::remove_if(
		cookies,
		[ & ]( const Cookie& held ) { return held.name == name && held.domain == domain && held.path == path; } ) };
	cookies.erase( removed.begin(), removed.end() );
}

void CookieOverlay::set( Cookie cookie )
{
	const std::scoped_lock lock { m_mutex };
	upsert( m_cookies, std::move( cookie ) );
}

void CookieOverlay::erase( const std::string_view name, const std::string_view domain, const std::string_view path )
{
	const std::scoped_lock lock { m_mutex };
	remove( m_cookies, name, domain, path );
}

void CookieOverlay::appendMatching(
	const std::string_view host,
	const std::string_view path,
	const bool secure,
	const std::chrono::system_clock::time_point now,
	std::vector< Cookie >& out ) const
{
	const std::scoped_lock lock { m_mutex };

	for ( const Cookie& cookie : m_cookies )
	{
		if ( matches( cookie, host, path, secure, now ) ) out.emplace_back( cookie );
	}
}

std::vector< Cookie > CookieOverlay::cookies() const
{
	const std::scoped_lock lock { m_mutex };
	return m_cookies;
}

CookieStore::CookieStore( CookiePersistence* persistence ) : m_persistence( persistence )
{}

void CookieStore::load()
{
	if ( m_persistence == nullptr ) return;

	m_persistence->pruneExpired();
	auto loaded { m_persistence->loadAll() };

	const auto now { std::chrono::system_clock::now() };
	const std::scoped_lock lock { m_mutex };

	for ( auto& [ name, domain, path, value, secure, host_only, expires ] : loaded )
	{
		if ( expires <= now ) continue;

		upsert(
			m_cookies,
			Cookie {
				.name = std::move( name ),
				.value = std::move( value ),
				.domain = std::move( domain ),
				.path = std::move( path ),
				.secure = secure,
				.host_only = host_only,
				.expires = expires } );
	}
}

void CookieStore::set( Cookie cookie )
{
	if ( !cookie.expires.has_value() ) return;

	const PersistedCookie persisted {
		.name = cookie.name,
		.domain = cookie.domain,
		.path = cookie.path,
		.value = cookie.value,
		.secure = cookie.secure,
		.host_only = cookie.host_only,
		.expires = *cookie.expires
	};

	{
		const std::scoped_lock lock { m_mutex };
		upsert( m_cookies, std::move( cookie ) );
	}

	if ( m_persistence != nullptr ) m_persistence->upsert( persisted );
}

void CookieStore::erase( const std::string_view name, const std::string_view domain, const std::string_view path )
{
	{
		const std::scoped_lock lock { m_mutex };
		remove( m_cookies, name, domain, path );
	}

	if ( m_persistence != nullptr ) m_persistence->erase( name, domain, path );
}

void CookieStore::appendMatching(
	const std::string_view host,
	const std::string_view path,
	const bool secure,
	const std::chrono::system_clock::time_point now,
	std::vector< Cookie >& out ) const
{
	const std::scoped_lock lock { m_mutex };

	for ( const Cookie& cookie : m_cookies )
	{
		if ( matches( cookie, host, path, secure, now ) ) out.emplace_back( cookie );
	}
}

std::vector< Cookie > CookieStore::cookies() const
{
	const std::scoped_lock lock { m_mutex };
	return m_cookies;
}

struct RequestTarget
{
	std::string host {};
	std::string path {};
	bool secure {};
};

static std::optional< RequestTarget > targetFor( const std::string_view url )
{
	const auto parsed { ada::parse< ada::url_aggregator >( url ) };

	if ( !parsed ) return std::nullopt;

	std::string path { parsed->get_pathname() };

	if ( path.empty() ) path = "/";

	return RequestTarget {
		.host = detail::normalizeHost( std::string { parsed->get_hostname() } ),
		.path = std::move( path ),
		.secure = parsed->get_protocol() == "https:"
	};
}

void applyRequestCookies(
	const std::string_view url,
	const CookieOverlay& overlay,
	const CookieStore& store,
	HttpHeaders& headers,
	const std::chrono::system_clock::time_point now )
{
	const auto target { targetFor( url ) };

	if ( !target ) return;

	std::vector< Cookie > matched {};
	overlay.appendMatching( target->host, target->path, target->secure, now, matched );
	const auto session_count { matched.size() };
	store.appendMatching( target->host, target->path, target->secure, now, matched );

	const auto shadowed { std::ranges::remove_if(
		matched.begin() + static_cast< std::ptrdiff_t >( session_count ),
		matched.end(),
		[ & ]( const Cookie& cookie )
		{
			return std::any_of(
				matched.begin(),
				matched.begin() + static_cast< std::ptrdiff_t >( session_count ),
				[ & ]( const Cookie& session_cookie ) { return session_cookie.name == cookie.name; } );
		} ) };
	matched.erase( shadowed.begin(), shadowed.end() );

	if ( matched.empty() ) return;

	std::ranges::stable_sort( matched, std::greater {}, []( const Cookie& cookie ) { return cookie.path.size(); } );

	std::string value {};

	for ( const Cookie& cookie : matched )
	{
		if ( !value.empty() ) value += "; ";
		value += cookie.name;
		value += '=';
		value += cookie.value;
	}

	headers.set( "Cookie", std::move( value ) );
}

ResponseCookies collectResponseCookies(
	const std::string_view url,
	const HttpHeaders& headers,
	const std::chrono::system_clock::time_point now )
{
	ResponseCookies collected {};
	const auto target { targetFor( url ) };

	if ( !target ) return collected;

	for ( const std::string_view header : headers.all( "set-cookie" ) )
	{
		const auto parsed { parseSetCookie( header ) };

		if ( !parsed ) continue;

		Cookie cookie { .name = parsed->name, .value = parsed->value };

		if ( parsed->domain.empty() )
		{
			cookie.domain = target->host;
			cookie.host_only = true;
		}
		else
		{
			std::string_view domain { parsed->domain };

			if ( domain.starts_with( '.' ) ) domain.remove_prefix( 1 );

			cookie.domain = detail::normalizeHost( std::string { domain } );

			if ( !cookieDomainMatches( target->host, cookie.domain, false ) ) continue;
		}

		cookie.path = parsed->path.empty() ? defaultCookiePath( target->path ) : parsed->path;
		cookie.secure = parsed->secure;

		if ( parsed->max_age.has_value() )
		{
			if ( *parsed->max_age <= 0 )
			{
				collected.removed.emplace_back( std::move( cookie ) );
				continue;
			}

			cookie.expires = now + std::chrono::seconds { *parsed->max_age };
		}
		else if ( parsed->expires.has_value() )
		{
			if ( *parsed->expires <= now )
			{
				collected.removed.emplace_back( std::move( cookie ) );
				continue;
			}

			cookie.expires = *parsed->expires;
		}

		if ( cookie.expires.has_value() )
			collected.persistent.emplace_back( std::move( cookie ) );
		else
			collected.session.emplace_back( std::move( cookie ) );
	}

	return collected;
}

} // namespace idhan::downloader
