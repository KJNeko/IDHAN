#include <folly/coro/BlockingWait.h>
#pragma once

#include <IDHANDownloader/DownloaderContext.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <format>
#include <functional>
#include <memory>
#include <string>
#include <thread>

#include "Fixtures.hpp"
#include "TestServer.hpp"

namespace idhan::test
{

struct Harness
{
	TestServer server {};
	ParserFixture parsers {};
	RecordingObserver observer {};
	RecordingLaneObserver lane_observer {};
	MemoryImportFactory imports {};
	MapSecrets secrets {};
	std::unique_ptr< downloader::DownloaderContext > context {};

	//! Low-level host tests use JS modules behind the current class/run entry contract.
	//! These adapters belong to the test harness, not the production parser loader.
	void prepareEntries()
	{
		for ( const auto& entry : std::filesystem::directory_iterator( parsers.root() ) )
		{
			if ( entry.path().extension() != ".js" ) continue;

			std::ifstream input { entry.path() };
			std::string source { std::istreambuf_iterator< char > { input }, std::istreambuf_iterator< char > {} };

			if ( source.contains( "export default" ) || source.contains( " as default" ) ) continue;

			source += R"JS(
export default class HostFixture {
    static metadata = {formatVersion: 1, sdkVersion: "1.0.0", id: ")JS"
			        + entry.path().stem().string()
			        + R"JS(", version: 1, options: [], handlers: [], discoveries: [], urlClasses: []};
}
export async function run(input, host) {
    const module = await import("./)JS"
			        + entry.path().filename().string() + R"JS(");
    const handler = module[input.operation];
    if (typeof handler !== "function") throw new Error(`Missing fixture operation: ${input.operation}`);
    return handler(input, host);
}
)JS";
			parsers.write( entry.path().filename().string(), source );
		}
	}

	void configureRaw(
		const std::string_view url_classes,
		downloader::DownloaderConfig config = {},
		downloader::SecretProvider* secret_provider = nullptr )
	{
		parsers.write( "url-classes.json", url_classes );

		prepareEntries();

		config.watch_scripts = false;
		config.parser_directory = parsers.root();
		config.url_classes = parsers.urlClassesPath();
		config.http_version = downloader::HttpVersion::HTTP_1_1;

		auto created { folly::coro::blockingWait(
			downloader::DownloaderContext::create(
				std::move( config ),
			downloader::DownloaderHost {
				.imports = &imports,
				.cookies = nullptr,
				.secrets = secret_provider ? secret_provider : &secrets,
					.lanes = &lane_observer } ) ) };
		if ( !created ) INFO( created.error() );
		REQUIRE( created.has_value() );
		context = std::move( *created );
	}

	void configure(
		const std::string_view routes,
		downloader::DownloaderConfig config = {},
		const std::string_view rate = "false",
		const std::string_view lane = "{}" )
	{
		configureRaw(
			std::format(
				R"({{"urlClasses":[{{"name":"test","script":"test.js","requestRate":{},"lane":{},"hosts":["127.0.0.1"],"routes":{}}}]}})",
				rate,
				lane,
				routes ),
			std::move( config ) );
	}

	std::shared_ptr< downloader::SessionContext > session( const std::string& root )
	{
		prepareEntries();
		return context->createSession( downloader::SessionOptions { .root_url = root, .observer = &observer } );
	}
};

inline void checkNoFailures( const RecordingObserver& observer )
{
	for ( const auto& [ work, error ] : observer.failures ) UNSCOPED_INFO( "work " << work << ": " << error );

	CHECK( observer.failures.empty() );
}

inline bool waitUntil(
	const std::function< bool() >& predicate,
	const std::chrono::milliseconds limit = std::chrono::seconds { 20 } )
{
	const auto deadline { std::chrono::steady_clock::now() + limit };

	while ( std::chrono::steady_clock::now() < deadline )
	{
		if ( predicate() ) return true;

		std::this_thread::sleep_for( std::chrono::milliseconds { 5 } );
	}

	return predicate();
}

} // namespace idhan::test
