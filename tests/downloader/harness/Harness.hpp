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

	void configureRaw( const std::string_view url_classes, downloader::DownloaderConfig config = {} )
	{
		parsers.write( "url-classes.json", url_classes );

		config.parser_directory = parsers.root();
		config.http_version = downloader::HttpVersion::HTTP_1_1;

		auto created { downloader::DownloaderContext::create(
			std::move( config ),
			downloader::DownloaderHost {
				.imports = &imports, .cookies = nullptr, .secrets = &secrets, .lanes = &lane_observer } ) };
		REQUIRE( created.has_value() );
		context = std::move( *created );
	}

	void configure(
		const std::string_view routes,
		downloader::DownloaderConfig config = {},
		const std::string_view rate = "false" )
	{
		configureRaw(
			std::format(
				R"({{"urlClasses":[{{"name":"test","script":"test.js","requestRate":{},"hosts":["127.0.0.1"],"routes":{}}}]}})",
				rate,
				routes ),
			std::move( config ) );
	}

	std::shared_ptr< downloader::SessionContext > session( const std::string& root )
	{
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
