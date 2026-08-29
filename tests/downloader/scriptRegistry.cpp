#include "scripts/ScriptRegistry.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

using namespace idhan::downloader;

static const std::filesystem::path source_root { IDHAN_SOURCE_DIR };

static std::unique_ptr< ScriptRegistry > examples()
{
	const std::filesystem::path parsers { IDHAN_DOWNLOADER_PARSER_DIR };
	auto registry { ScriptRegistry::create( parsers / "url-classes.json", parsers ) };
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
	CHECK( ( *gallery )->export_name == "gallery" );

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

TEST_CASE( "A host with no URL class routes to nothing without erroring", "[downloader][registry]" )
{
	const auto registry { examples() };
	const auto route { registry->route( "https://example.com/nowhere" ) };

	REQUIRE( route.has_value() );
	CHECK_FALSE( route->has_value() );
}

TEST_CASE( "Two classes claiming one URL is an error, not a first match", "[downloader][registry]" )
{
	const auto testdata { source_root / "tests/downloader/testdata" };
	auto registry { ScriptRegistry::create( testdata / "ambiguous-url-classes.json", testdata ) };
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
	CHECK( gelbooru.rate->requests == 10 );
	CHECK( gelbooru.rate->seconds == 1 );
	CHECK_FALSE( gelbooru.group.has_value() );

	const auto gelbooru_cdn { registry->laneSettings( "img4.gelbooru.com" ) };
	REQUIRE( gelbooru_cdn.rate.has_value() );
	CHECK( gelbooru_cdn.rate->requests == 5 );
	CHECK( gelbooru_cdn.rate->seconds == 1 );
	CHECK_FALSE( gelbooru_cdn.group.has_value() );

	const auto hyshare { registry->laneSettings( "norate.example.com" ) };
	CHECK_FALSE( hyshare.rate.has_value() );

	const auto sibling { registry->laneSettings( "rate.example.com" ) };
	CHECK_FALSE( sibling.rate.has_value() );

	const auto unknown { registry->laneSettings( "nowhere.example" ) };
	CHECK_FALSE( unknown.group.has_value() );
}

TEST_CASE( "Lanes inherit the default rate unless explicitly unthrottled", "[downloader][registry]" )
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
            {"name":"unthrottled","script":"site.js","requestRate":false,
             "hosts":["unthrottled.example"],"routes":[{"export":"page"}]}
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

	const auto unthrottled { ( *registry )->laneSettings( "unthrottled.example" ) };
	CHECK_FALSE( unthrottled.rate.has_value() );

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
            "requestRate":{"requests":2,"seconds":1,"shareSubdomains":true},
            "lane":{"bandwidth":1048576,"keepAlive":90,"errorBackoff":90,"concurrency":12,"httpVersion":"2"},
            "hosts":["site.example",
                     {"host":"pooled.example","requestRate":{"requests":3,"seconds":4,
                      "shareSubdomains":true,"poolSubdomains":true}},
                     {"host":"cdn.example","requestRate":false,
                     "lane":{"group":"assets","bandwidth":0}}],
            "routes":[{"export":"post","pathPrefix":"/p/"}]}]})";
	}

	auto registry { ScriptRegistry::create( directory / "url-classes.json", directory ) };
	REQUIRE( registry.has_value() );

	const auto site { ( *registry )->laneSettings( "site.example" ) };
	CHECK( site.bytes_per_second == 1048576 );
	CHECK( site.keep_alive == std::chrono::seconds { 90 } );
	CHECK( site.error_backoff == std::chrono::seconds { 90 } );
	CHECK( site.concurrency == 12 );
	CHECK( site.http_version == HttpVersion::HTTP_2 );
	CHECK_FALSE( site.group.has_value() );

	const auto subdomain { ( *registry )->laneSettings( "img4.site.example" ) };
	REQUIRE( subdomain.rate.has_value() );
	CHECK( subdomain.rate->requests == 2 );
	CHECK( subdomain.rate->seconds == 1 );
	CHECK_FALSE( subdomain.group.has_value() );

	const auto pooled { ( *registry )->laneSettings( "pooled.example" ) };
	REQUIRE( pooled.rate.has_value() );
	CHECK( pooled.rate->requests == 3 );
	CHECK( pooled.rate->seconds == 4 );
	REQUIRE( pooled.group.has_value() );
	CHECK( *pooled.group == "pooled.example" );

	const auto pooled_subdomain { ( *registry )->laneSettings( "img.pooled.example" ) };
	REQUIRE( pooled_subdomain.group.has_value() );
	CHECK( *pooled_subdomain.group == "pooled.example" );

	const auto cdn { ( *registry )->laneSettings( "cdn.example" ) };
	CHECK_FALSE( cdn.rate.has_value() );
	CHECK( cdn.bytes_per_second == 0 );
	REQUIRE( cdn.group.has_value() );
	CHECK( *cdn.group == "assets" );

	std::error_code error {};
	std::filesystem::remove_all( directory, error );
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
	CHECK( registry.error().contains( "hosts" ) );

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
