#include "scripts/ScriptRegistry.hpp"
#include <IDHANDownloader/DownloaderConfig.hpp>
#include <IDHANDownloader/UrlComparison.hpp>
#include "harness/Fixtures.hpp"
#include "UrlStorageImportSink.hpp"

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

TEST_CASE( "A host with no URL class uses automatic file inspection", "[downloader][registry]" )
{
	const auto registry { examples() };
	const auto route { registry->route( "https://example.com/nowhere" ) };

	REQUIRE( route.has_value() );
	REQUIRE( route->has_value() );
	CHECK( ( **route ).download );
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
	CHECK( gelbooru_cdn.rate->requests == 2 );
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

TEST_CASE( "URL comparison sorts query names without changing the resource", "[downloader][url-comparison]" )
{
	CHECK( urlComparisonKey( "https://example.com/file?b=2&a=1&" ) == "https://example.com/file?a=1&b=2" );
	CHECK(
		urlComparisonKey( "https://example.com/file?b=2&a=%20#preview" )
	    == urlComparisonKey( "https://example.com/file?a=+&b=2" ) );
	CHECK( urlComparisonKey( "https://example.com/file?" ) == "https://example.com/file" );
	CHECK(
		urlComparisonKey( "https://example.com/file?ex=1&%68m=2&ex=3&", { "ex", "hm" } )
	    == "https://example.com/file" );
	CHECK(
		urlComparisonKey( "https://example.com/file?hm=2&is=1&ex=3", { "ex", "hm" } )
	    == "https://example.com/file?is=1" );
	CHECK( urlComparisonKey( "https://example.com/file?EX=3", { "ex" } ) == "https://example.com/file?EX=3" );
	CHECK( urlComparisonKey( "https://example.com/file?b=2&a=2&a=1" ) == "https://example.com/file?a=2&a=1&b=2" );
	CHECK(
		urlComparisonKey( "https://example.com/file?a=2&a=1" )
	    != urlComparisonKey( "https://example.com/file?a=1&a=2" ) );
	CHECK( urlComparisonKey( "https://example.com/File?a=1" ) != urlComparisonKey( "https://example.com/file?a=1" ) );
	CHECK( urlComparisonBase( "https://example.com/file#preview?x=1" ) == "https://example.com/file" );
}

TEST_CASE( "Ephemeral query declarations are validated", "[downloader][url-comparison]" )
{
	idhan::test::ParserFixture fixture {};
	for ( const auto& value : { "true", "null", "42", "\"ex\"", "[42]", "[\"\"]" } )
	{
		fixture.write(
			"url-classes.json",
			std::format(
				R"({{"urlClasses":[{{"name":"test","script":"test.js","hosts":["example.com"],"routes":[{{"export":"page"}}],"ephemeralQuery":{}}}]}})",
				value ) );
		const auto registry { ScriptRegistry::create( fixture.urlClassesPath(), fixture.root() ) };
		CHECK_FALSE( registry.has_value() );
		if ( !registry ) CHECK( registry.error().contains( "ephemeralQuery" ) );
	}
}

TEST_CASE( "The Discord comparison policy preserves the supplied URL", "[downloader][url-comparison]" )
{
	const auto registry { ScriptRegistry::create(
		source_root / "IDHANDownloader/premade/url-classes/discord.json", source_root / "IDHANDownloader/premade" ) };
	REQUIRE( registry.has_value() );
	const std::string url {
		"https://cdn.discordapp.com/attachments/1538534554133069854/1564013740284842005/Screenshot_20261025_163204.png?ex=6adfb8cd&is=6ade674d&hm=1663a3f6c22e6c3c16d08d0c17d5f2d8cca516fb7816a0dccf02fbc07e6d5b5f&"
	};
	const auto key { ( *registry )->comparisonKey( url ) };
	CHECK( key == urlComparisonBase( url ) + "?is=6ade674d" );
	CHECK( url.contains( "?ex=6adfb8cd" ) );
	CHECK( ( *registry )->ephemeralQuery( "https://other.example/file?ex=1" ).empty() );
	CHECK( ( *registry )->ephemeralQuery( "https://cdn.discordapp.com.evil.example/file?ex=1" ).empty() );
}

TEST_CASE( "Ephemeral parameters remain available to route matching", "[downloader][registry][url-comparison]" )
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
	CHECK( ( *registry )->comparisonKey( first ) == ( *registry )->comparisonKey( second ) );
	CHECK( ( *registry )->comparisonKey( first ) == "https://example.com/file?extra=1&id=42" );

	// An unlisted parameter does not constrain a route, but it still identifies the resource.
	const std::string changed { "https://example.com/file?token=first&id=42&extra=2" };
	const auto changed_route { ( *registry )->route( changed ) };
	REQUIRE( changed_route.has_value() );
	REQUIRE( changed_route->has_value() );
	CHECK( ( **changed_route ).export_name == "first" );
	CHECK( ( *registry )->comparisonKey( first ) != ( *registry )->comparisonKey( changed ) );

	// Being ephemeral does not waive a route's requirement for the parameter to be present.
	const auto absent { ( *registry )->route( "https://example.com/file?id=42&extra=1" ) };
	REQUIRE( absent.has_value() );
	REQUIRE( absent->has_value() );
	CHECK( ( **absent ).download );
}

TEST_CASE( "Storage query sorting preserves signed parameter bytes", "[downloader][url-storage]" )
{
	CHECK(
		sortUrlQueryParameters( "https://example.com/file?z=%2f&a=%20&a=+&ex=token%2Braw&hm=hash#view" )
	    == "https://example.com/file?a=%20&a=+&ex=token%2Braw&hm=hash&z=%2f#view" );
	CHECK( sortUrlQueryParameters( "https://example.com/file?b=2&%61=1&" ) == "https://example.com/file?%61=1&b=2" );
	CHECK( sortUrlQueryParameters( "https://example.com/file?b=2&a&b=1" ) == "https://example.com/file?a&b=2&b=1" );
	CHECK(
		sortUrlQueryParameters( "https://example.com/file#view?b=2&a=1" ) == "https://example.com/file#view?b=2&a=1" );
	CHECK( sortUrlQueryParameters( "https://example.com/file" ) == "https://example.com/file" );
}

TEST_CASE( "URL classes control storage sorting independently of comparisons", "[downloader][url-storage]" )
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
		CHECK( ( *registry )->storageUrl( url ) == ( enabled ? sorted : url ) );
		CHECK( ( *registry )->comparisonKey( url ) == "https://example.com/file?a=1&ex=keep&z=2" );
		CHECK(
			( *registry )->storageUrl( "https://unconfigured.example/file?z=2&a=1" )
		    == "https://unconfigured.example/file?a=1&z=2" );
	}
}

TEST_CASE( "Storage sorting rejects non-boolean settings", "[downloader][url-storage]" )
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

TEST_CASE( "Storage sorting uses the matched class and redirect destination", "[downloader][url-storage]" )
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
	CHECK( ( *registry )->storageUrl( "https://example.com/raw?z=2&a=1" ) == "https://example.com/raw?z=2&a=1" );
	CHECK( ( *registry )->storageUrl( "https://example.com/sorted?z=2&a=1" ) == "https://example.com/sorted?a=1&z=2" );
	CHECK(
		( *registry )->storageUrl( "https://example.com/automatic?z=2&a=1" )
	    == "https://example.com/automatic?z=2&a=1" );

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
	REQUIRE( sink.finish( ImportMetadata { .final_url = final } ).has_value() );
	REQUIRE( imports.metadata.size() == 1 );
	CHECK( imports.metadata.front().final_url == expected );
}
