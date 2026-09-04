#include "cookies/CookieStore.hpp"

#include <catch2/catch_test_macros.hpp>

#include "http/HttpMessage.hpp"

using namespace idhan::downloader;

static HttpHeaders setCookies( const std::vector< std::string >& values )
{
	HttpHeaders headers {};

	for ( const std::string& value : values ) headers.add( "Set-Cookie", value );

	return headers;
}

TEST_CASE( "Cookie domain and path matching follow the usual rules", "[downloader][cookies]" )
{
	CHECK( cookieDomainMatches( "example.com", "example.com", true ) );
	CHECK_FALSE( cookieDomainMatches( "img.example.com", "example.com", true ) );
	CHECK( cookieDomainMatches( "img.example.com", "example.com", false ) );
	CHECK_FALSE( cookieDomainMatches( "notexample.com", "example.com", false ) );

	CHECK( cookiePathMatches( "/anything", "/" ) );
	CHECK( cookiePathMatches( "/posts/1", "/posts" ) );
	CHECK_FALSE( cookiePathMatches( "/postscript", "/posts" ) );

	CHECK( defaultCookiePath( "/posts/1" ) == "/posts" );
	CHECK( defaultCookiePath( "/posts" ) == "/" );
	CHECK( defaultCookiePath( "/" ) == "/" );
}

TEST_CASE( "An expiry decides whether a cookie is shared or stays in the session", "[downloader][cookies]" )
{
	const auto now { std::chrono::system_clock::now() };
	const auto collected { collectResponseCookies(
		"https://example.com/posts/1",
		setCookies( { "keeps=1; Max-Age=3600", "session=2", "gone=3; Max-Age=0" } ),
		now ) };

	REQUIRE( collected.persistent.size() == 1 );
	CHECK( collected.persistent.front().name == "keeps" );
	CHECK( collected.persistent.front().host_only );
	CHECK( collected.persistent.front().path == "/posts" );

	REQUIRE( collected.session.size() == 1 );
	CHECK( collected.session.front().name == "session" );
	CHECK_FALSE( collected.session.front().expires.has_value() );

	REQUIRE( collected.removed.size() == 1 );
	CHECK( collected.removed.front().name == "gone" );
}

TEST_CASE( "A Set-Cookie for someone else's domain is dropped", "[downloader][cookies]" )
{
	const auto collected {
		collectResponseCookies( "https://example.com/", setCookies( { "evil=1; Domain=other.example" } ) )
	};

	CHECK( collected.persistent.empty() );
	CHECK( collected.session.empty() );
}

TEST_CASE( "A leading dot on a cookie Domain is ignored", "[downloader][cookies]" )
{
	const auto collected {
		collectResponseCookies( "https://example.com/", setCookies( { "shared=1; Domain=.example.com" } ) )
	};

	REQUIRE( collected.session.size() == 1 );
	CHECK( collected.session.front().domain == "example.com" );
	CHECK_FALSE( collected.session.front().host_only );

	CookieStore store {};
	CookieOverlay overlay {};
	overlay.set( collected.session.front() );

	HttpHeaders origin_headers {};
	applyRequestCookies( "https://example.com/", overlay, store, origin_headers );
	CHECK( origin_headers.get( "cookie" ) == "shared=1" );

	HttpHeaders subdomain_headers {};
	applyRequestCookies( "https://img.example.com/", overlay, store, subdomain_headers );
	CHECK( subdomain_headers.get( "cookie" ) == "shared=1" );
}

TEST_CASE( "The store only ever holds cookies that carry an expiry", "[downloader][cookies]" )
{
	CookieStore store {};
	store.set( Cookie { .name = "session", .value = "1", .domain = "example.com" } );
	CHECK( store.cookies().empty() );

	store.set(
		Cookie { .name = "kept",
	             .value = "1",
	             .domain = "example.com",
	             .expires = std::chrono::system_clock::now() + std::chrono::hours { 1 } } );
	CHECK( store.cookies().size() == 1 );
}

TEST_CASE( "A session cookie shadows the shared one of the same name", "[downloader][cookies]" )
{
	CookieStore store {};
	CookieOverlay overlay {};

	store.set(
		Cookie { .name = "auth",
	             .value = "global",
	             .domain = "example.com",
	             .expires = std::chrono::system_clock::now() + std::chrono::hours { 1 } } );
	overlay.set( Cookie { .name = "auth", .value = "session", .domain = "example.com" } );

	HttpHeaders headers {};
	applyRequestCookies( "https://example.com/", overlay, store, headers );

	CHECK( headers.get( "cookie" ) == "auth=session" );
}

TEST_CASE( "Cookies are sent longest path first and skip mismatched scope", "[downloader][cookies]" )
{
	CookieStore store {};
	CookieOverlay overlay {};

	overlay.set( Cookie { .name = "root", .value = "1", .domain = "example.com", .path = "/" } );
	overlay.set( Cookie { .name = "deep", .value = "2", .domain = "example.com", .path = "/posts/hot" } );
	overlay.set( Cookie { .name = "mid", .value = "3", .domain = "example.com", .path = "/posts" } );
	overlay.set( Cookie { .name = "elsewhere", .value = "4", .domain = "example.com", .path = "/admin" } );
	overlay.set( Cookie { .name = "tls", .value = "5", .domain = "example.com", .path = "/", .secure = true } );

	HttpHeaders headers {};
	applyRequestCookies( "http://example.com/posts/hot/1", overlay, store, headers );

	CHECK( headers.get( "cookie" ) == "deep=2; mid=3; root=1" );
}

TEST_CASE( "An expired cookie is never sent", "[downloader][cookies]" )
{
	CookieStore store {};
	CookieOverlay overlay {};
	const auto now { std::chrono::system_clock::now() };

	overlay.set(
		Cookie { .name = "stale", .value = "1", .domain = "example.com", .expires = now - std::chrono::hours { 1 } } );

	HttpHeaders headers {};
	applyRequestCookies( "https://example.com/", overlay, store, headers, now );

	CHECK_FALSE( headers.contains( "cookie" ) );
}

namespace
{
class RecordingPersistence final : public CookiePersistence
{
  public:

	std::vector< PersistedCookie > rows {};
	std::vector< std::string > erased {};
	std::size_t prunes {};

	std::vector< PersistedCookie > loadAll() override { return rows; }

	void upsert( const PersistedCookie& cookie ) override { rows.emplace_back( cookie ); }

	void erase( const std::string_view name, std::string_view, std::string_view ) override
	{
		erased.emplace_back( name );
	}

	void pruneExpired() override { ++prunes; }
};
} // namespace

TEST_CASE( "The store writes through to its persistence and loads back from it", "[downloader][cookies]" )
{
	RecordingPersistence persistence {};
	persistence.rows.emplace_back(
		PersistedCookie {
			.name = "kept",
			.domain = "example.com",
			.path = "/",
			.value = "1",
			.expires = std::chrono::system_clock::now() + std::chrono::hours { 1 } } );
	persistence.rows.emplace_back(
		PersistedCookie {
			.name = "stale",
			.domain = "example.com",
			.path = "/",
			.value = "2",
			.expires = std::chrono::system_clock::now() - std::chrono::hours { 1 } } );

	CookieStore store { &persistence };
	store.load();

	CHECK( persistence.prunes == 1 );
	REQUIRE( store.cookies().size() == 1 );
	CHECK( store.cookies().front().name == "kept" );

	store.set( Cookie { .name = "session", .value = "3", .domain = "example.com" } );
	CHECK( persistence.rows.size() == 2 );

	store.erase( "kept", "example.com", "/" );
	REQUIRE( persistence.erased.size() == 1 );
	CHECK( persistence.erased.front() == "kept" );
	CHECK( store.cookies().empty() );
}
