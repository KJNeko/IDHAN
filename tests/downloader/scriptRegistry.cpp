#include "scripts/ScriptRegistry.hpp"
#include <IDHANDownloader/DownloaderConfig.hpp>
#include <IDHANDownloader/urls/UrlNormalization.hpp>
#include "harness/Fixtures.hpp"
#include "imports/UrlStorageImportSink.hpp"

#include <folly/coro/BlockingWait.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <filesystem>
#include <fstream>

using namespace idhan::downloader;

static const std::filesystem::path source_root { IDHAN_SOURCE_DIR };

static std::unique_ptr< ScriptRegistry > examples()
{
	const std::filesystem::path parsers { IDHAN_DOWNLOADER_PARSER_DIR };
	auto registry { ScriptRegistry::create( {}, parsers ) };
	REQUIRE( registry.has_value() );
	return std::move( *registry );
}

TEST_CASE( "URLs route to the class and export that claims them", "[downloader][registry]" )
{
	const auto registry { examples() };

	const auto gallery { registry->route( "https://gelbooru.com/index.php?page=post&s=list&tags=cat" ) };
	REQUIRE( gallery.has_value() );
	REQUIRE( gallery->has_value() );
	CHECK( ( *gallery )->url_class == "gelbooru" );
	CHECK( ( *gallery )->export_name == "search" );

	const auto post { registry->route( "https://gelbooru.com/index.php?page=post&s=view&id=1" ) };
	REQUIRE( post.has_value() );
	REQUIRE( post->has_value() );
	CHECK( ( *post )->export_name == "post" );

	const auto image { registry->route( "https://i.pximg.net/img-original/img/2020/01/01/x.png" ) };
	REQUIRE( image.has_value() );
	REQUIRE( image->has_value() );
	CHECK( ( *image )->url_class == "pixiv-image" );
	CHECK( ( *image )->script.filename() == "pixiv.js" );
}

TEST_CASE( "A host with no URL class uses automatic file inspection", "[downloader][registry]" )
{
	const auto registry { examples() };
	const auto route { registry->route( "https://example.com/nowhere" ) };

	REQUIRE( route.has_value() );
	REQUIRE( route->has_value() );
	CHECK( ( **route ).export_name == "__discover" );
}

TEST_CASE(
	"Follow routing downloads unclaimed URLs without changing root discovery",
	"[downloader][registry][follow-routing]" )
{
	const auto registry { examples() };
	const std::string source { "https://somerandomdomain.com/myrandomimage.jpg" };
	const auto root_e { registry->route( source ) };
	REQUIRE( root_e.has_value() );
	REQUIRE( root_e.value().has_value() );
	CHECK( root_e.value().value().url_class == "discovery" );

	const auto followed_e { registry->route( source, false ) };
	REQUIRE( followed_e.has_value() );
	REQUIRE( followed_e.value().has_value() );
	const auto& followed { followed_e.value().value() };
	CHECK( followed.download );
	CHECK( followed.script.empty() );
	CHECK( followed.discovery_scripts.empty() );

	const auto next_root_e { registry->route( source ) };
	REQUIRE( next_root_e.has_value() );
	REQUIRE( next_root_e.value().has_value() );
	CHECK( next_root_e.value().value().url_class == "discovery" );

	const auto page_e { registry->route( "https://gelbooru.com/index.php?page=post&s=view&id=1", false ) };
	REQUIRE( page_e.has_value() );
	REQUIRE( page_e.value().has_value() );
	CHECK_FALSE( page_e.value().value().download );
	CHECK( page_e.value().value().url_class == "gelbooru" );
	CHECK( page_e.value().value().export_name == "post" );
}

TEST_CASE( "Two classes claiming one URL is an error, not a first match", "[downloader][registry]" )
{
	idhan::test::ParserFixture fixture;
	fixture.write( "first.js", "export function post() {}" );
	fixture.write( "second.js", "export function post() {}" );
	fixture.write( "url-classes.json", R"JSON({"urlClasses":[
		{"name":"first","script":"first.js","hosts":["example.com"],"routes":[{"export":"post","pathPrefix":"/posts/"}]},
		{"name":"second","script":"second.js","hosts":["example.com"],"routes":[{"export":"post","pathPrefix":"/posts/"}]}
	]})JSON" );
	auto registry { ScriptRegistry::create( fixture.urlClassesPath(), fixture.root() ) };
	REQUIRE( registry.has_value() );

	const auto route { ( *registry )->route( "https://example.com/posts/1" ) };
	REQUIRE_FALSE( route.has_value() );
	CHECK( route.error().contains( "matches both" ) );
}

TEST_CASE( "Hostnames are normalised before matching", "[downloader][registry]" )
{
	const auto testdata { source_root / "tests/downloader/testdata" };
	auto registry { ScriptRegistry::create( testdata / "executor-url-classes.json", testdata ) };
	REQUIRE( registry.has_value() );

	const auto route { ( *registry )->route( "https://EXECUTOR.example/gallery" ) };
	REQUIRE( route.has_value() );
	REQUIRE( route->has_value() );
	CHECK( ( *route )->export_name == "gallery" );
}

TEST_CASE( "An explicit host request rate overrides a shared parent rate", "[downloader][registry]" )
{
	const auto registry { examples() };

	const auto gelbooru { registry->laneSettings( "gelbooru.com" ) };
	REQUIRE( gelbooru.rate.has_value() );
	CHECK( gelbooru.rate->requests > 0 );
	CHECK( gelbooru.rate->seconds > 0 );
	CHECK_FALSE( gelbooru.group.has_value() );

	const auto gelbooru_cdn { registry->laneSettings( "img4.gelbooru.com" ) };
	CHECK( gelbooru_cdn.concurrency > 0 );
	CHECK_FALSE( gelbooru_cdn.group.has_value() );
	CHECK( gelbooru_cdn.status_retries.at( 404 ).count > 0 );
	CHECK( gelbooru_cdn.status_retries.at( 404 ).wait > std::chrono::seconds::zero() );
	CHECK( gelbooru.status_retries.empty() );

	const auto hyshare { registry->laneSettings( "norate.example.com" ) };
	CHECK_FALSE( hyshare.rate.has_value() );

	const auto sibling { registry->laneSettings( "rate.example.com" ) };
	CHECK_FALSE( sibling.rate.has_value() );

	const auto unknown { registry->laneSettings( "nowhere.example" ) };
	CHECK_FALSE( unknown.group.has_value() );
}

TEST_CASE( "Lanes inherit the default rate unless explicitly unlimited", "[downloader][registry]" )
{
	const auto directory { std::filesystem::temp_directory_path() / "idhan-registry-default-rate-test" };
	std::filesystem::create_directories( directory );
	{
		std::ofstream script { directory / "site.js" };
		script << "export function page() {}\n";

		std::ofstream classes { directory / "url-classes.json" };
		classes << R"({"urlClasses":[
            {"name":"default","script":"site.js","hosts":["default.example"],
             "routes":[{"export":"page"}]},
            {"name":"unlimited","script":"site.js","requestRate":false,
             "hosts":["unlimited.example"],"routes":[{"export":"page"}]}
        ]})";
	}

	const DownloaderConfig config {};
	LaneSettings defaults { .rate = config.default_rate };
	const auto registry { ScriptRegistry::create( directory / "url-classes.json", directory, std::move( defaults ) ) };
	REQUIRE( registry.has_value() );

	const auto inherited { ( *registry )->laneSettings( "default.example" ) };
	REQUIRE( inherited.rate.has_value() );
	CHECK( *inherited.rate == ( RequestRate { .requests = 1, .seconds = 5 } ) );

	const auto unknown { ( *registry )->laneSettings( "unknown.example" ) };
	REQUIRE( unknown.rate.has_value() );
	CHECK( *unknown.rate == ( RequestRate { .requests = 1, .seconds = 5 } ) );

	const auto unlimited { ( *registry )->laneSettings( "unlimited.example" ) };
	CHECK_FALSE( unlimited.rate.has_value() );

	std::error_code error {};
	std::filesystem::remove_all( directory, error );
}

TEST_CASE( "Subdomains may inherit settings with or without sharing a lane", "[downloader][registry]" )
{
	const auto directory { std::filesystem::temp_directory_path() / "idhan-registry-lane-test" };
	std::filesystem::create_directories( directory );
	{
		std::ofstream script { directory / "site.js" };
		script << "export function post() {}\n";

		std::ofstream classes { directory / "url-classes.json" };
		classes << R"({"urlClasses":[{
            "name":"site","script":"site.js",
            "requestRate":{"requests":2,"seconds":1,"spread":true,"concurrency":4,"shareSubdomains":true},
            "lane":{"keepAlive":90,"errorBackoff":90,"concurrency":12,"httpVersion":"2",
                    "backoffStatuses":[429]},
            "hosts":["site.example",
                     {"host":"pooled.example","requestRate":{"requests":3,"seconds":4,
                      "shareSubdomains":true,"poolSubdomains":true}},
                     {"host":"cdn.example","requestRate":false,
                     "lane":{"group":"assets","backoffStatuses":[]}}],
            "routes":[{"export":"post","pathPrefix":"/p/"}]}]})";
	}

	auto registry { ScriptRegistry::create( directory / "url-classes.json", directory ) };
	REQUIRE( registry.has_value() );

	const auto site { ( *registry )->laneSettings( "site.example" ) };
	CHECK( site.keep_alive == std::chrono::seconds { 90 } );
	CHECK( site.error_backoff == std::chrono::seconds { 90 } );
	CHECK( site.concurrency == 12 );
	CHECK( site.http_version == HttpVersion::HTTP_2 );
	CHECK_FALSE( site.group.has_value() );
	CHECK( site.backoff_statuses.contains( 429 ) );
	CHECK_FALSE( site.backoff_statuses.contains( 500 ) );

	const auto subdomain { ( *registry )->laneSettings( "img4.site.example" ) };
	REQUIRE( subdomain.rate.has_value() );
	CHECK( subdomain.rate->requests == 2 );
	CHECK( subdomain.rate->seconds == 1 );
	CHECK( subdomain.rate->spread );
	CHECK( subdomain.rate->concurrency == 4 );
	CHECK_FALSE( subdomain.group.has_value() );
	CHECK( subdomain.backoff_statuses.contains( 429 ) );

	const auto pooled { ( *registry )->laneSettings( "pooled.example" ) };
	REQUIRE( pooled.rate.has_value() );
	CHECK( pooled.rate->requests == 3 );
	CHECK( pooled.rate->seconds == 4 );
	CHECK_FALSE( pooled.rate->spread );
	CHECK( pooled.rate->concurrency == 1 );
	REQUIRE( pooled.group.has_value() );
	CHECK( *pooled.group == "pooled.example" );

	const auto pooled_subdomain { ( *registry )->laneSettings( "img.pooled.example" ) };
	REQUIRE( pooled_subdomain.group.has_value() );
	CHECK( *pooled_subdomain.group == "pooled.example" );

	const auto cdn { ( *registry )->laneSettings( "cdn.example" ) };
	CHECK_FALSE( cdn.rate.has_value() );
	REQUIRE( cdn.group.has_value() );
	CHECK( *cdn.group == "assets" );
	CHECK( cdn.backoff_statuses.empty() );

	std::error_code error {};
	std::filesystem::remove_all( directory, error );
}

TEST_CASE( "Wildcard hosts apply lane settings to storage subdomains", "[downloader][registry]" )
{
	idhan::test::ParserFixture fixture;
	fixture.write( "site.js", "export function post() {}" );
	fixture.write( "url-classes.json", R"JSON({"urlClasses":[{
		"name":"mega-storage","script":"site.js",
		"hosts":[{"host":"*.userstorage.mega.co.nz","requestRate":false,"lane":{"concurrency":4}}],
		"routes":[{"export":"post","pathPrefix":"/"}]
	}]})JSON" );
	const auto registry_e { ScriptRegistry::create( fixture.urlClassesPath(), fixture.root() ) };
	REQUIRE( registry_e.has_value() );
	const auto& registry { registry_e.value() };

	for ( const auto host : { "gfs1.userstorage.mega.co.nz", "gfs2.userstorage.mega.co.nz" } )
	{
		const auto settings { registry->laneSettings( host ) };
		CHECK_FALSE( settings.rate.has_value() );
		CHECK( settings.concurrency == 4 );
		CHECK_FALSE( settings.group.has_value() );
	}

	for ( const auto host : { "other.mega.co.nz", "notuserstorage.mega.co.nz", "userstorage.mega.co.nz.example" } )
	{
		CHECK( registry->laneSettings( host ).concurrency == 0 );
	}
}

TEST_CASE( "Backoff status configuration requires HTTP error codes", "[downloader][registry]" )
{
	const std::string statuses { GENERATE( "null", "true", "[\"429\"]", "[200]", "[600]" ) };
	idhan::test::ParserFixture fixture {};
	fixture.write( "site.js", "export function page() {}" );
	fixture.write(
		"url-classes.json",
		std::format(
			R"({{"urlClasses":[{{"name":"test","script":"site.js","hosts":["test.example"],"routes":[{{"export":"page"}}],"lane":{{"backoffStatuses":{}}}}}]}})",
			statuses ) );
	const auto registry_e { ScriptRegistry::create( fixture.urlClassesPath(), fixture.root() ) };
	REQUIRE_FALSE( registry_e.has_value() );
	CHECK( registry_e.error().contains( "backoffStatuses" ) );
}

TEST_CASE( "A retry wait must be a number of seconds", "[downloader][registry]" )
{
	const std::string wait { GENERATE( "true", "\"30\"", "-1", "1.5" ) };
	idhan::test::ParserFixture fixture {};
	fixture.write( "site.js", "export function page() {}" );
	fixture.write(
		"url-classes.json",
		std::format(
			R"({{"urlClasses":[{{"name":"test","script":"site.js","hosts":["test.example"],"routes":[{{"export":"page"}}],"retry":[{{"code":404,"count":1,"wait":{}}}]}}]}})",
			wait ) );
	const auto registry_e { ScriptRegistry::create( fixture.urlClassesPath(), fixture.root() ) };
	REQUIRE_FALSE( registry_e.has_value() );
	CHECK( registry_e.error().contains( "retry wait" ) );
}

TEST_CASE( "A malformed URL class file is rejected with a reason", "[downloader][registry]" )
{
	const auto directory { std::filesystem::temp_directory_path() / "idhan-registry-bad-test" };
	std::filesystem::create_directories( directory );
	{
		std::ofstream classes { directory / "url-classes.json" };
		classes << R"({"urlClasses":[{"name":"broken","script":"missing.js","hosts":[],"routes":[]}]})";
	}

	const auto registry { ScriptRegistry::create( directory / "url-classes.json", directory ) };
	REQUIRE_FALSE( registry.has_value() );
	CHECK( registry.error().contains( "routes" ) );

	std::error_code error {};
	std::filesystem::remove_all( directory, error );
}

TEST_CASE( "Request rate concurrency requires a positive integer", "[downloader][registry]" )
{
	const auto invalid { GENERATE( "0", "-1", "1.5", "\"2\"", "true" ) };
	const auto directory { std::filesystem::temp_directory_path() / "idhan-registry-invalid-concurrency-test" };
	std::filesystem::create_directories( directory );
	{
		std::ofstream script { directory / "site.js" };
		script << "export function page() {}\n";
		std::ofstream classes { directory / "url-classes.json" };
		classes << R"({"urlClasses":[{"name":"site","script":"site.js",
            "requestRate":{"requests":8,"seconds":1,"concurrency":)"
		        << invalid << R"(},
            "hosts":["site.example"],"routes":[{"export":"page"}]}]})";
	}

	const auto registry_e { ScriptRegistry::create( directory / "url-classes.json", directory ) };
	REQUIRE_FALSE( registry_e.has_value() );
	CHECK( registry_e.error().contains( "concurrency must be a positive integer" ) );
	std::error_code error {};
	std::filesystem::remove_all( directory, error );
}

TEST_CASE( "Spread rate settings reject non-boolean values", "[downloader][registry]" )
{
	const auto directory { std::filesystem::temp_directory_path() / "idhan-registry-invalid-spread-test" };
	std::filesystem::create_directories( directory );
	{
		std::ofstream script { directory / "site.js" };
		script << "export function page() {}\n";
		std::ofstream classes { directory / "url-classes.json" };
		classes << R"({"urlClasses":[{"name":"site","script":"site.js",
            "rateLimit":{"requests":8,"seconds":1,"spread":1},
            "hosts":["site.example"],"routes":[{"export":"page"}]}]})";
	}

	const auto registry_e { ScriptRegistry::create( directory / "url-classes.json", directory ) };
	REQUIRE_FALSE( registry_e.has_value() );
	CHECK( registry_e.error().contains( "spread must be a boolean" ) );
	std::error_code error {};
	std::filesystem::remove_all( directory, error );
}

TEST_CASE( "Pooling subdomains requires subdomain inheritance", "[downloader][registry]" )
{
	const auto directory { std::filesystem::temp_directory_path() / "idhan-registry-invalid-pool-test" };
	std::filesystem::create_directories( directory );
	{
		std::ofstream script { directory / "site.js" };
		script << "export function page() {}\n";

		std::ofstream classes { directory / "url-classes.json" };
		classes << R"({"urlClasses":[{"name":"site","script":"site.js",
            "requestRate":{"requests":1,"seconds":1,"poolSubdomains":true},
            "hosts":["site.example"],"routes":[{"export":"page"}]}]})";
	}

	const auto registry { ScriptRegistry::create( directory / "url-classes.json", directory ) };
	REQUIRE_FALSE( registry.has_value() );
	CHECK( registry.error().contains( "poolSubdomains requires shareSubdomains" ) );

	std::error_code error {};
	std::filesystem::remove_all( directory, error );
}

TEST_CASE( "Unclaimed URLs are inspected without host or extension rules", "[downloader][registry][direct]" )
{
	const auto testdata { source_root / "tests/downloader/testdata" };
	auto registry { ScriptRegistry::create( testdata / "executor-url-classes.json", testdata ) };
	REQUIRE( registry.has_value() );
	for ( const auto url :
	      { "https://img4.gelbooru.com/images/file",
	        "https://other.example/image.PNG?x=1",
	        "https://other.example/download?id=1" } )
	{
		const auto route { ( *registry )->route( url ) };
		REQUIRE( route.has_value() );
		REQUIRE( route->has_value() );
		CHECK( ( **route ).download );
		CHECK( ( **route ).script.empty() );
	}
	const auto page { ( *registry )->route( "https://executor.example/gallery" ) };
	REQUIRE( page.has_value() );
	REQUIRE( page->has_value() );
	CHECK_FALSE( ( **page ).download );
	const auto unsupported { ( *registry )->route( "ftp://other.example/file.png" ) };
	REQUIRE( unsupported.has_value() );
	CHECK_FALSE( unsupported->has_value() );
}

static std::string normalized( const std::string_view url, const UrlRules& rules = {} )
{
	auto parsed_e { parseUrl( url ) };
	REQUIRE( parsed_e.has_value() );
	return applyUrlRules( std::move( parsed_e.value() ), rules ).url;
}

static std::string normalized( const ScriptRegistry& registry, const std::string_view url )
{
	const auto normalized_e { registry.normalizeUrl( url ) };
	REQUIRE( normalized_e.has_value() );
	return normalized_e.value().url;
}

TEST_CASE( "The default normalization sorts query names", "[downloader][url-normalization]" )
{
	CHECK( normalized( "https://example.com/file?b=2&a=1&" ) == "https://example.com/file?a=1&b=2" );
	CHECK( normalized( "https://example.com/file?" ) == "https://example.com/file" );
	CHECK( normalized( "https://example.com/file?b=2&a=2&a=1" ) == "https://example.com/file?a=2&a=1&b=2" );
	CHECK( normalized( "https://example.com/file?a=2&a=1" ) != normalized( "https://example.com/file?a=1&a=2" ) );
	CHECK( normalized( "https://example.com/File?a=1" ) != normalized( "https://example.com/file?a=1" ) );
	CHECK( normalized( "HTTPS://Example.COM:443/file" ) == "https://example.com/file" );
	CHECK(
		normalized( "https://example.com/file?b=2&a=1", { .sort_query = false } )
	    == "https://example.com/file?b=2&a=1" );
	CHECK( normalized( "https://example.com/file?b=2&a=1#view" ) == "https://example.com/file?a=1&b=2#view" );
	CHECK( normalized( "https://example.com/file#view?b=2&a=1" ) == "https://example.com/file#view?b=2&a=1" );
	CHECK( normalized( "https://example.com/file??b=2&a=1" ) == "https://example.com/file??b=2&a=1" );
	CHECK_FALSE( parseUrl( "https://exa mple.com/" ).has_value() );
	CHECK_FALSE( parseUrl( "" ).has_value() );
}

TEST_CASE( "URLs without a scheme default to HTTPS", "[downloader][url-normalization]" )
{
	CHECK( withDefaultScheme( "example.com/post" ) == "https://example.com/post" );
	CHECK( withDefaultScheme( "//example.com/post" ) == "https://example.com/post" );
	CHECK( withDefaultScheme( "example.com:8080/post" ) == "https://example.com:8080/post" );
	CHECK(
		withDefaultScheme( "example.com/r?to=https://other.example" )
	    == "https://example.com/r?to=https://other.example" );
	CHECK( withDefaultScheme( "http://example.com/post" ) == "http://example.com/post" );
	CHECK( withDefaultScheme( "file:///tmp/file.png" ) == "file:///tmp/file.png" );
	CHECK( normalized( "example.com/post?b=2&a=1" ) == "https://example.com/post?a=1&b=2" );
}

TEST_CASE( "Query rules preserve the encoding of kept fields", "[downloader][url-normalization]" )
{
	CHECK(
		normalized( "https://example.com/file?z=%2f&a=%20&a=+&ex=token%2Braw&hm=hash#view" )
	    == "https://example.com/file?a=%20&a=+&ex=token%2Braw&hm=hash&z=%2f#view" );
	CHECK( normalized( "https://example.com/file?b=2&%61=1&" ) == "https://example.com/file?%61=1&b=2" );
	CHECK( normalized( "https://example.com/file?b=2&a&b=1" ) == "https://example.com/file?a&b=2&b=1" );

	const UrlRules rules { .remove_query = { "tags" }, .ephemeral_query = { "ex", "hm" } };
	auto parsed_e { parseUrl( "https://example.com/file?hm=2&tags=x&is=1&%65x=3&tags=y" ) };
	REQUIRE( parsed_e.has_value() );
	const auto result { applyUrlRules( std::move( parsed_e.value() ), rules ) };
	CHECK( result.url == "https://example.com/file?is=1" );
	CHECK( result.ephemeral_query == "hm=2&%65x=3" );
	CHECK( normalized( "https://example.com/file?EX=3", rules ) == "https://example.com/file?EX=3" );
	CHECK( normalized( "https://example.com/file?ex=1&hm=2", rules ) == "https://example.com/file" );
}

TEST_CASE( "Query rule declarations are validated", "[downloader][url-normalization]" )
{
	idhan::test::ParserFixture fixture {};
	for ( const std::string field : { "ephemeralQuery", "removeQuery" } )
	{
		for ( const auto& value : { "true", "null", "42", "\"ex\"", "[42]", "[\"\"]" } )
		{
			fixture.write(
				"url-classes.json",
				std::format(
					R"({{"urlClasses":[{{"name":"test","script":"test.js","hosts":["example.com"],"routes":[{{"export":"page"}}],"{}":{}}}]}})",
					field,
					value ) );
			const auto registry { ScriptRegistry::create( fixture.urlClassesPath(), fixture.root() ) };
			CHECK_FALSE( registry.has_value() );
			if ( !registry ) CHECK( registry.error().contains( field ) );
		}
	}
}

TEST_CASE( "Hyshare is unlimited, including hosts learned through discovery", "[downloader][registry][rate]" )
{
	LaneSettings defaults {};
	defaults.rate = RequestRate { .requests = 1, .seconds = 5 };
	const auto registry {
		ScriptRegistry::create( {}, std::filesystem::path { IDHAN_DOWNLOADER_PARSER_DIR }, std::move( defaults ) )
	};
	REQUIRE( registry.has_value() );

	CHECK( ( *registry )->laneSettings( "share.example" ).rate.has_value() );

	REQUIRE( folly::coro::blockingWait( ( *registry )->learnHost( "share.example", "hyshare" ) ).has_value() );
	CHECK_FALSE( ( *registry )->laneSettings( "share.example" ).rate.has_value() );
	CHECK( ( *registry )->laneSettings( "other.example" ).rate.has_value() );

	CHECK_FALSE(
		folly::coro::blockingWait( ( *registry )->learnHost( "share.example", "missing-class" ) ).has_value() );
}

TEST_CASE( "Discovered hosts route directly to their parser across reloads", "[downloader][registry][discovery]" )
{
	const auto registry { examples() };
	const std::string hash( 64, 'a' );
	const auto post_url { "https://hyshare.example.com/view/" + hash };
	const auto initial_e { registry->route( post_url ) };
	REQUIRE( initial_e.has_value() );
	REQUIRE( initial_e.value().has_value() );
	CHECK( initial_e.value().value().url_class == "discovery" );

	REQUIRE( folly::coro::blockingWait( registry->learnHost( "hyshare.example.com", "hyshare" ) ).has_value() );
	for ( const bool reload : { false, true } )
	{
		if ( reload ) REQUIRE( registry->updateParsers().has_value() );

		const auto post_e { registry->route( post_url ) };
		REQUIRE( post_e.has_value() );
		const auto& post { post_e.value() };
		REQUIRE( post.has_value() );
		CHECK( post.value().url_class == "hyshare" );
		CHECK( post.value().export_name == "post" );
		CHECK( post.value().parameters[ "hash" ].asString() == hash );
		CHECK( post.value().configured_domain == "hyshare.example.com" );
		CHECK( post.value().script.filename() == "hyshare.js" );

		const auto gallery_e { registry->route( "https://hyshare.example.com/gallery/test" ) };
		REQUIRE( gallery_e.has_value() );
		REQUIRE( gallery_e.value().has_value() );
		CHECK( gallery_e.value().value().export_name == "gallery" );

		// Unclaimed paths on a confirmed host should not probe discovery again.
		const std::vector< std::string > paths { "/", "/file/" + hash, "/unrecognized" };
		for ( const auto& path : paths )
		{
			const auto file_e { registry->route( "https://hyshare.example.com" + path ) };
			REQUIRE( file_e.has_value() );
			REQUIRE( file_e.value().has_value() );
			CHECK( file_e.value().value().download );
		}

		const auto other_e { registry->route( "https://other.example.com/view/" + hash ) };
		REQUIRE( other_e.has_value() );
		REQUIRE( other_e.value().has_value() );
		CHECK( other_e.value().value().url_class == "discovery" );
	}
}

TEST_CASE( "Learning a host does not override an explicitly configured one", "[downloader][registry][rate]" )
{
	LaneSettings defaults {};
	defaults.rate = RequestRate { .requests = 1, .seconds = 5 };
	const auto registry {
		ScriptRegistry::create( {}, std::filesystem::path { IDHAN_DOWNLOADER_PARSER_DIR }, std::move( defaults ) )
	};
	REQUIRE( registry.has_value() );

	const auto before { ( *registry )->laneSettings( "e621.net" ) };
	REQUIRE( before.rate.has_value() );
	REQUIRE( folly::coro::blockingWait( ( *registry )->learnHost( "e621.net", "hyshare" ) ).has_value() );

	const auto after { ( *registry )->laneSettings( "e621.net" ) };
	REQUIRE( after.rate.has_value() );
	CHECK( after.rate->seconds == before.rate->seconds );
}

TEST_CASE( "Discord expiry parameters are ephemeral", "[downloader][url-normalization]" )
{
	const auto registry { ScriptRegistry::create( {}, std::filesystem::path { IDHAN_DOWNLOADER_PARSER_DIR } ) };
	REQUIRE( registry.has_value() );
	const std::string base {
		"https://cdn.discordapp.com/attachments/1538534554133069854/1564013740284842005/Screenshot_20261025_163204.png"
	};
	const auto result_e {
		( *registry )
			->normalizeUrl(
				base + "?ex=6adfb8cd&is=6ade674d&hm=1663a3f6c22e6c3c16d08d0c17d5f2d8cca516fb7816a0dccf02fbc07e6d5b5f&" )
	};
	REQUIRE( result_e.has_value() );
	CHECK( result_e.value().url == base + "?is=6ade674d" );
	CHECK(
		result_e.value().ephemeral_query
	    == "ex=6adfb8cd&hm=1663a3f6c22e6c3c16d08d0c17d5f2d8cca516fb7816a0dccf02fbc07e6d5b5f" );
	CHECK( normalized( **registry, "https://other.example/file?ex=1" ) == "https://other.example/file?ex=1" );
	CHECK(
		normalized( **registry, "https://cdn.discordapp.com.evil.example/file?ex=1" )
	    == "https://cdn.discordapp.com.evil.example/file?ex=1" );
}

TEST_CASE( "Repeated path slashes are collapsed", "[downloader][url-normalization]" )
{
	const auto registry { ScriptRegistry::create( {}, std::filesystem::path { IDHAN_DOWNLOADER_PARSER_DIR } ) };
	REQUIRE( registry.has_value() );
	CHECK(
		normalized( **registry, "https://img4.gelbooru.com//images/c8/60/c860e1c482aa23c0d2a6e8b8727246ec.jpeg" )
	    == "https://img4.gelbooru.com/images/c8/60/c860e1c482aa23c0d2a6e8b8727246ec.jpeg" );
	CHECK( normalized( **registry, "https://example.com///a//b/?b=2&a=1" ) == "https://example.com/a/b/?a=1&b=2" );
	CHECK( normalized( **registry, "https://example.com/a?next=//b" ) == "https://example.com/a?next=//b" );
	CHECK( normalized( **registry, "https://example.com/a#//b" ) == "https://example.com/a#//b" );
	CHECK( normalized( **registry, "https://example.com" ) == "https://example.com/" );
}

TEST_CASE( "A host pointsTo another host when normalized", "[downloader][url-normalization]" )
{
	idhan::test::ParserFixture fixture {};
	fixture.write( "url-classes.json", R"JSON({"urlClasses":[{
		"name":"test", "script":"test.js", "removeQuery":["utm"], "routes":[{"export":"post", "path":"/post"}],
		"hosts":["example.com", "cdn.example.com", {"host":"old.example.com", "pointsTo":"CDN.example.com"}]
	}]})JSON" );
	const auto registry { ScriptRegistry::create( fixture.urlClassesPath(), fixture.root() ) };
	REQUIRE( registry.has_value() );
	CHECK(
		normalized( **registry, "https://old.example.com//a.png?utm=1&b=2" ) == "https://cdn.example.com/a.png?b=2" );
	CHECK( normalized( **registry, "https://cdn.example.com/a.png" ) == "https://cdn.example.com/a.png" );
	CHECK( normalized( **registry, "https://sub.old.example.com/a.png" ) == "https://sub.old.example.com/a.png" );
	CHECK( normalized( **registry, "https://example.com/a.png" ) == "https://example.com/a.png" );

	const auto premade { ScriptRegistry::create( {}, std::filesystem::path { IDHAN_DOWNLOADER_PARSER_DIR } ) };
	REQUIRE( premade.has_value() );
	for ( const std::string host : { "img2", "img3", "img4" } )
		CHECK(
			normalized(
				**premade, "https://" + host + ".gelbooru.com/images/26/04/260452485936f47b3d5bc0a0346cb62f.png" )
		    == "https://img4.gelbooru.com/images/26/04/260452485936f47b3d5bc0a0346cb62f.png" );
}

TEST_CASE( "Invalid pointsTo hosts are rejected", "[downloader][url-normalization]" )
{
	const auto [ host, target, error ] { GENERATE(
		table< std::string, std::string, std::string >( {
			{ "*.old.example.com", "cdn.example.com", "cannot apply to subdomains" },
			{ "old.example.com", "OLD.example.com", "points to itself" },
			{ "old.example.com", "cdn.example.com/path", "pointsTo must be a bare hostname" },
			{ "old.example.com", "", "pointsTo must be a bare hostname" },
		} ) ) };
	idhan::test::ParserFixture fixture {};
	fixture.write(
		"url-classes.json",
		R"JSON({"urlClasses":[{"name":"test", "script":"test.js", "routes":[{"export":"post", "path":"/post"}],
			"hosts":[{"host":")JSON"
	        + host + R"JSON(", "pointsTo":")JSON" + target + R"JSON("}]}]})JSON" );
	const auto registry { ScriptRegistry::create( fixture.urlClassesPath(), fixture.root() ) };
	REQUIRE_FALSE( registry.has_value() );
	CHECK( registry.error().contains( error ) );
}

TEST_CASE( "Booru post links from searches normalize to the post", "[downloader][url-normalization]" )
{
	const auto registry { ScriptRegistry::create( {}, std::filesystem::path { IDHAN_DOWNLOADER_PARSER_DIR } ) };
	REQUIRE( registry.has_value() );
	CHECK(
		normalized( **registry, "https://gelbooru.com/index.php?page=post&s=view&id=14339807&tags=sort%3Ascore" )
	    == "https://gelbooru.com/index.php?id=14339807&page=post&s=view" );
	// Tags identify a search page, so only post links lose them.
	CHECK(
		normalized( **registry, "https://gelbooru.com/index.php?tags=wolf&page=post&s=list" )
	    == "https://gelbooru.com/index.php?page=post&s=list&tags=wolf" );
	CHECK( normalized( **registry, "https://e621.net/posts/1?q=wolf" ) == "https://e621.net/posts/1" );
	CHECK( normalized( **registry, "https://e621.net/posts?tags=wolf" ) == "https://e621.net/posts?tags=wolf" );
}

TEST_CASE( "Ephemeral parameters remain available to route matching", "[downloader][registry][url-normalization]" )
{
	idhan::test::ParserFixture fixture {};
	fixture.write( "url-classes.json", R"JSON({"urlClasses":[{
		"name":"test", "script":"test.js", "hosts":["example.com"],
		"ephemeralQuery":["token"], "routes":[
			{"export":"first", "path":"/file", "query":{"token":"first"}},
			{"export":"second", "path":"/file", "query":{"token":"second"}}
		]
	}]})JSON" );
	const auto registry { ScriptRegistry::create( fixture.urlClassesPath(), fixture.root() ) };
	REQUIRE( registry.has_value() );
	const std::string first { "https://example.com/file?token=first&id=42&extra=1" };
	const std::string second { "https://example.com/file?extra=1&id=42&token=second" };
	const auto first_route { ( *registry )->route( first ) };
	REQUIRE( first_route.has_value() );
	REQUIRE( first_route->has_value() );
	CHECK( ( **first_route ).export_name == "first" );
	const auto second_route { ( *registry )->route( second ) };
	REQUIRE( second_route.has_value() );
	REQUIRE( second_route->has_value() );
	CHECK( ( **second_route ).export_name == "second" );
	CHECK( normalized( **registry, first ) == normalized( **registry, second ) );
	CHECK( normalized( **registry, first ) == "https://example.com/file?extra=1&id=42" );

	// An unlisted parameter does not constrain a route, but it still identifies the resource.
	const std::string changed { "https://example.com/file?token=first&id=42&extra=2" };
	const auto changed_route { ( *registry )->route( changed ) };
	REQUIRE( changed_route.has_value() );
	REQUIRE( changed_route->has_value() );
	CHECK( ( **changed_route ).export_name == "first" );
	CHECK( normalized( **registry, first ) != normalized( **registry, changed ) );

	// Being ephemeral does not waive a route's requirement for the parameter to be present.
	const auto absent { ( *registry )->route( "https://example.com/file?id=42&extra=1" ) };
	REQUIRE( absent.has_value() );
	REQUIRE( absent->has_value() );
	CHECK( ( **absent ).download );
}

TEST_CASE( "Removed parameters apply to the class or only its matching pattern", "[downloader][url-normalization]" )
{
	idhan::test::ParserFixture fixture {};
	fixture.write( "url-classes.json", R"JSON({"urlClasses":[{
		"name":"test", "script":"test.js", "hosts":["example.com"], "removeQuery":["utm"], "routes":[
			{"export":"post", "path":"/post", "removeQuery":["tags"]},
			{"export":"search", "path":"/search"}
		]
	}]})JSON" );
	const auto registry { ScriptRegistry::create( fixture.urlClassesPath(), fixture.root() ) };
	REQUIRE( registry.has_value() );
	CHECK( normalized( **registry, "https://example.com/post?utm=x&tags=a&id=1" ) == "https://example.com/post?id=1" );
	CHECK( normalized( **registry, "https://example.com/search?utm=x&tags=a" ) == "https://example.com/search?tags=a" );
	CHECK( normalized( **registry, "https://example.com/other?tags=a&utm=x" ) == "https://example.com/other?tags=a" );
	CHECK_FALSE( ( *registry )->normalizeUrl( "https://exa mple.com/post" ).has_value() );
}

TEST_CASE( "URL classes control query sorting", "[downloader][url-normalization]" )
{
	idhan::test::ParserFixture fixture {};
	for ( const std::string setting : { "", ",\"sortQueryParameters\":true", ",\"sortQueryParameters\":false" } )
	{
		fixture.write(
			"url-classes.json",
			std::format(
				R"({{"urlClasses":[{{"name":"test","script":"test.js","hosts":["example.com"],"routes":[{{"export":"page"}}]{}}}]}})",
				setting ) );
		const auto registry { ScriptRegistry::create( fixture.urlClassesPath(), fixture.root() ) };
		REQUIRE( registry.has_value() );
		const std::string url { "https://example.com/file?z=2&a=1&ex=keep#view" };
		const std::string sorted { "https://example.com/file?a=1&ex=keep&z=2#view" };
		const bool enabled { !setting.contains( "false" ) };
		CHECK( normalized( **registry, url ) == ( enabled ? sorted : url ) );
		CHECK(
			normalized( **registry, "https://unconfigured.example/file?z=2&a=1" )
		    == "https://unconfigured.example/file?a=1&z=2" );
	}
}

TEST_CASE( "Query sorting rejects non-boolean settings", "[downloader][url-normalization]" )
{
	idhan::test::ParserFixture fixture {};
	for ( const auto setting : { "null", "1", "\"false\"", "[]", "{}" } )
	{
		fixture.write(
			"url-classes.json",
			std::format(
				R"({{"urlClasses":[{{"name":"test","script":"test.js","hosts":["example.com"],"routes":[{{"export":"page"}}],"sortQueryParameters":{}}}]}})",
				setting ) );
		const auto registry { ScriptRegistry::create( fixture.urlClassesPath(), fixture.root() ) };
		REQUIRE_FALSE( registry.has_value() );
		CHECK( registry.error().contains( "sortQueryParameters" ) );
	}
}

TEST_CASE( "Query sorting uses the matched class and redirect destination", "[downloader][url-normalization]" )
{
	idhan::test::ParserFixture fixture {};
	fixture.write( "url-classes.json", R"JSON({"urlClasses":[
		{"name":"raw","script":"test.js","hosts":["example.com"],
		 "routes":[{"export":"page","path":"/raw"}],"sortQueryParameters":false},
		{"name":"sorted","script":"test.js","hosts":["example.com"],
		 "routes":[{"export":"page","path":"/sorted"}],"sortQueryParameters":true}
	]})JSON" );
	const auto registry { ScriptRegistry::create( fixture.urlClassesPath(), fixture.root() ) };
	REQUIRE( registry.has_value() );
	CHECK( normalized( **registry, "https://example.com/raw?z=2&a=1" ) == "https://example.com/raw?z=2&a=1" );
	CHECK( normalized( **registry, "https://example.com/sorted?z=2&a=1" ) == "https://example.com/sorted?a=1&z=2" );
	CHECK(
		normalized( **registry, "https://example.com/automatic?z=2&a=1" ) == "https://example.com/automatic?z=2&a=1" );

	idhan::test::MemoryImportFactory imports {};
	auto inner { imports.open( ImportRequest { .url = "https://source.example/file?z=2&a=1" } ) };
	REQUIRE( inner.has_value() );
	UrlStorageImportSink sink { std::move( *inner ), **registry };
	std::string final { "https://other.example/file?z=2&a=1&hm=signed" };
	std::string expected { "https://other.example/file?a=1&hm=signed&z=2" };
	SECTION( "An unconfigured destination sorts by default" )
	{}
	SECTION( "A redirect to an opted-out class preserves its query order" )
	{
		final = "https://example.com/raw?z=2&a=1&hm=signed";
		expected = final;
	}
	REQUIRE( folly::coro::blockingWait( sink.finish( ImportMetadata { .effective_url = final } ) ).has_value() );
	REQUIRE( imports.metadata.size() == 1 );
	CHECK( imports.metadata.front().effective_url == expected );
}

TEST_CASE( "Recognized pages without parsers skip file inspection", "[downloader][registry][direct]" )
{
	const auto registry { examples() };
	for ( const auto* url :
	      { "https://x.com/example/status/123",
	        "https://twitter.com/example/status/123?s=20",
	        "https://mobile.twitter.com/example/status/123/photo/1" } )
	{
		INFO( url );
		const auto route_e { registry->route( url ) };
		REQUIRE_FALSE( route_e.has_value() );
		CHECK( route_e.error().contains( "Skipped URL: no parser found" ) );
	}
}

TEST_CASE( "A parser can handle a page recognized by a parser-free manifest", "[downloader][registry]" )
{
	idhan::test::ParserFixture fixture;
	fixture.write( "site.js", "export function post() {}" );
	fixture.write( "url-classes.json", R"JSON({"urlClasses":[
		{"id":"pages","hosts":["example.com"],"url_patterns":[{"name":"Post","patterns":[{"pathname":"/posts/:id"}]}]},
		{"name":"parser","script":"site.js","hosts":["example.com"],"routes":[{"export":"post","pathPrefix":"/posts/"}]}
	]})JSON" );
	const auto registry_e { ScriptRegistry::create( fixture.urlClassesPath(), fixture.root() ) };
	REQUIRE( registry_e.has_value() );
	const auto& registry { registry_e.value() };
	const auto route_e { registry->route( "https://example.com/posts/123" ) };
	REQUIRE( route_e.has_value() );
	const auto& route { route_e.value() };
	REQUIRE( route.has_value() );
	CHECK( route.value().export_name == "post" );
	CHECK_FALSE( route.value().download );
}
