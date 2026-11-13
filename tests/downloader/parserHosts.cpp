#include <folly/coro/BlockingWait.h>
#include <folly/coro/Baton.h>
#include <folly/ScopeGuard.h>
#include <folly/executors/CPUThreadPoolExecutor.h>

#include <chrono>
#include <future>
#include <catch2/catch_test_macros.hpp>
#include "scripts/ScriptRegistry.hpp"

namespace idhan::test
{

class MemoryParserHosts final : public downloader::ParserHostPersistence
{
  public:
	std::vector< downloader::ParserHost > hosts;
	std::size_t writes {};
	bool fail {};
	folly::coro::Baton* discovery_gate {};
	std::promise< void > discovery_started {};

	folly::coro::Task< std::expected< std::vector< downloader::ParserHost >, std::string > > loadAll() override
	{
		if ( fail ) co_return std::unexpected( "storage unavailable" );

		co_return hosts;
	}

	folly::coro::Task< std::expected< downloader::ParserHost, std::string > > discover(
		std::string host,
		std::string parser ) override
	{
		++writes;
		if ( discovery_gate != nullptr )
		{
			discovery_started.set_value();
			co_await *discovery_gate;
		}

		if ( fail ) co_return std::unexpected( "storage unavailable" );

		for ( const auto& entry : hosts )
			if ( entry.host == host ) co_return entry;

		hosts.push_back( { std::move( host ), std::move( parser ), true } );
		co_return hosts.back();
	}
};

static std::unique_ptr< downloader::ScriptRegistry > parserHostRegistry( MemoryParserHosts& persistence )
{
	auto registry_e { downloader::ScriptRegistry::create( {}, IDHAN_DOWNLOADER_PARSER_DIR ) };
	REQUIRE( registry_e.has_value() );
	auto registry { std::move( registry_e.value() ) };
	REQUIRE( folly::coro::blockingWait( registry->loadParserHosts( &persistence ) ).has_value() );
	return registry;
}

TEST_CASE( "Discovered parser hosts are persisted and restored in a new registry", "[downloader][hosts]" )
{
	MemoryParserHosts persistence;
	const auto registry { parserHostRegistry( persistence ) };
	REQUIRE( folly::coro::blockingWait( registry->learnHost( "share.example", "hyshare" ) ).has_value() );
	REQUIRE( folly::coro::blockingWait( registry->learnHost( "share.example", "hyshare" ) ).has_value() );
	REQUIRE( persistence.hosts.size() == 1 );
	CHECK( persistence.hosts.front().host == "share.example" );
	CHECK( persistence.hosts.front().parser == "hyshare" );
	CHECK( persistence.hosts.front().auto_discovered );
	CHECK( persistence.writes == 1 );

	const auto restored { parserHostRegistry( persistence ) };
	const auto route_e { restored->route( "https://share.example/view/" + std::string( 64, 'a' ) ) };
	REQUIRE( route_e.has_value() );
	REQUIRE( route_e.value().has_value() );
	CHECK( route_e.value().value().export_name == "post" );
	CHECK( persistence.writes == 1 );

	restored->removeParserHost( "share.example" );
	persistence.hosts.clear();
	const auto removed_e { restored->route( "https://share.example/view/" + std::string( 64, 'a' ) ) };
	REQUIRE( removed_e.has_value() );
	REQUIRE( removed_e.value().has_value() );
	CHECK( removed_e.value().value().url_class == "discovery" );
}

TEST_CASE( "Discovery preserves user-added host provenance", "[downloader][hosts]" )
{
	MemoryParserHosts persistence;
	persistence.hosts.push_back( { "share.example", "hyshare", false } );
	const auto registry { parserHostRegistry( persistence ) };
	REQUIRE( folly::coro::blockingWait( registry->learnHost( "share.example", "hyshare" ) ).has_value() );
	CHECK_FALSE( persistence.hosts.front().auto_discovered );
	CHECK( persistence.writes == 0 );
}

TEST_CASE( "Failed host persistence does not install an ephemeral association", "[downloader][hosts]" )
{
	MemoryParserHosts persistence;
	const auto registry { parserHostRegistry( persistence ) };
	persistence.fail = true;
	CHECK_FALSE( folly::coro::blockingWait( registry->learnHost( "share.example", "hyshare" ) ).has_value() );
	const auto route_e { registry->route( "https://share.example/view/" + std::string( 64, 'a' ) ) };
	REQUIRE( route_e.has_value() );
	REQUIRE( route_e.value().has_value() );
	CHECK( route_e.value().value().url_class == "discovery" );
}

TEST_CASE( "Parser host discovery suspends while persistence waits", "[downloader][hosts][host-scheduling]" )
{
	MemoryParserHosts persistence;
	auto registry { parserHostRegistry( persistence ) };
	folly::CPUThreadPoolExecutor executor { 1 };
	folly::coro::Baton release;
	persistence.discovery_gate = &release;
	auto started { persistence.discovery_started.get_future() };
	auto discovering {
		folly::coro::co_withExecutor( &executor, registry->learnHost( "share.example", "hyshare" ) ).start()
	};
	const auto cleanup { folly::makeGuard( [ & ] { release.post(); } ) };
	REQUIRE( started.wait_for( std::chrono::seconds { 2 } ) == std::future_status::ready );
	CHECK_FALSE( discovering.isReady() );

	std::promise< void > available;
	auto available_future { available.get_future() };
	executor.add( [ &available ] { available.set_value(); } );
	CHECK( available_future.wait_for( std::chrono::seconds { 2 } ) == std::future_status::ready );

	release.post();
	REQUIRE( std::move( discovering ).get().has_value() );
	CHECK( persistence.writes == 1 );
	REQUIRE( persistence.hosts.size() == 1 );
	CHECK( persistence.hosts.front().parser == "hyshare" );
}

} // namespace idhan::test
