#include <folly/coro/BlockingWait.h>
#include <catch2/catch_test_macros.hpp>
#include "harness/Harness.hpp"

namespace idhan::test
{
TEST_CASE( "Direct imports consume binary chunks without HTTP and abort partial files", "[downloader][import-direct]" )
{
    Harness harness;
    bool abort {};
    bool abandon {};
    SECTION( "Finish streamed bytes" ) {}
    SECTION( "Abort partial bytes" ) { abort = true; }
    SECTION( "Discard an unfinished stream when its parser exits" ) { abandon = true; }
    harness.parsers.write( "test.js", R"JS(
export default class Parser {
    static metadata = {id: 'direct', sdkVersion: '1.1.0', version: 1,
        options: [], handlers: [], discoveries: [], urlClasses: []};
}
export async function run(input, host) {
    await host.importDirect({id: 1, operation: 'open', filename: 'test.bin',
        urls: [{url: input.url, type: 'post'}], tags: {}, metadata: {}});
    await host.importDirect({id: 1, operation: 'write'}, new Uint8Array([0, 1, 255]).subarray(1));
    await host.importDirect({id: 1, operation: 'write'}, new Uint8Array([2]).buffer);
    if (input.url.includes('abandon')) return;
    await host.importDirect({id: 1, operation: input.url.includes('abort') ? 'abort' : 'finish'});
}
)JS" );
    harness.configure( R"([{"pattern":"/.*","export":"run"}])" );
    auto session { harness.session( harness.server.url( abandon ? "/abandon" : abort ? "/abort" : "/finish" ) ) };
    REQUIRE( session->submit( session->rootUrl() ).has_value() );
    REQUIRE( waitUntil( [&] { return session->idle(); } ) );
	folly::coro::blockingWait( session->wait() );
	folly::coro::blockingWait( harness.context->shutdown() );
	CHECK( harness.imports.async_writes.load() == 2 );
	if ( abort || abandon )
    {
        CHECK( harness.imports.stored.empty() );
        CHECK( harness.imports.aborted.load() == 1 );
    }
    else
    {
        REQUIRE( harness.imports.stored.size() == 1 );
        CHECK( harness.imports.stored[ 0 ].second == std::string( "\x01\xff\x02", 3 ) );
        CHECK( harness.imports.metadata[ 0 ].size == 3 );
        CHECK( harness.imports.metadata[ 0 ].filename == "test.bin" );
        checkNoFailures( harness.observer );
    }
}
} // namespace idhan::test
