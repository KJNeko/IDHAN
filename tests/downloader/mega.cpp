#include <folly/coro/BlockingWait.h>
#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include "harness/Harness.hpp"

namespace idhan::test
{
TEST_CASE( "MEGA decrypts shared files in QuickJS and rejects corrupt content", "[downloader][mega]" )
{
    Harness harness;
    bool corrupt {};
    bool folder {};
    bool selected {};
    bool retry {};
    bool configured {};
    bool invalid_connections {};
    bool invalid_chunk_size {};
	bool repeated {};
	bool reordered {};
	SECTION( "Valid decrypted stream" ) {}
    SECTION( "Integrity failure" ) { corrupt = true; }
    SECTION( "Transient MEGA API error" ) { retry = true; }
    SECTION( "Shared folder" ) { folder = true; }
    SECTION( "Selected file in a shared folder" ) { selected = true; }
	SECTION( "The shared parser imports from a later session" )
	{
		repeated = true;
	}
	SECTION( "Configured transfer options" ) { configured = true; }
    SECTION( "Configured folder transfer options" ) { folder = true; configured = true; }
    SECTION( "Connection count must be positive" ) { invalid_connections = true; }
    SECTION( "Chunk size must be an integer" ) { invalid_chunk_size = true; }
	SECTION( "Chunks finishing out of order keep every connection busy" )
	{
		reordered = true;
	}
	std::ifstream input { std::filesystem::path { IDHAN_SOURCE_DIR } / "tests/downloader/fixtures/mega-download.json" };
    const std::string fixture { std::istreambuf_iterator<char> { input }, std::istreambuf_iterator<char> {} };
    REQUIRE_FALSE( fixture.empty() );
    std::filesystem::copy_file( std::filesystem::path { IDHAN_DOWNLOADER_PARSER_DIR } / "mega.js", harness.parsers.root() / "mega.js" );
    harness.parsers.write( "test.js", "const fixture = " + fixture + R"JS(;
export default class Test {
    static metadata = {id: 'mega-fixture', version: 1, sdkVersion: '1.1.0', options: [], handlers: [], discoveries: [], urlClasses: []};
}
export async function run(input, host) {
    const url = input.url.includes('folder') || input.url.includes('selected')
        ? 'https://mega.nz/folder/example#' + fixture.folderKey + (input.url.includes('selected') ? '/file/child' : '')
        : 'https://mega.nz/file/example#' + fixture.key;
    let apiRequests = 0;
    const transfers = [];
    let active = 0;
    let finished = 0;
    globalThis.fetch = async (url, options) => {
        if (url.startsWith('https://g.api.mega.co.nz/')) {
            if (input.url.includes('retry') && apiRequests++ === 0) return new Response('[-3]');
            const command = JSON.parse(options.body)[0];
            if (command.a === 'f') return new Response(JSON.stringify([fixture.folderResponse]));
            if (command.a !== 'g') throw new Error('Unexpected MEGA API command');
            return new Response(JSON.stringify([{at: fixture.at, s: fixture.bytes.length, g: 'https://download.test/file'}]));
        }
        if (!url.startsWith('https://download.test/file/')) throw new Error('Unexpected transfer');
        const bytes = new Uint8Array(fixture.bytes);
        if (input.url.includes('corrupt')) bytes[0] ^= 1;
        // Ranged fetch path, e.g. https://download.test/file/4-7
        const range = /\/(\d+)-(\d+)$/.exec(url);
        if (!range) throw new Error('Unexpected transfer range: ' + url);
        const first = Number(range[1]);
        const last = Number(range[2]);
        if (input.url.includes('reordered')) {
            transfers.push({active: ++active, afterFirstFinish: finished > 0});
            // Earlier chunks answer later, so every window of chunks finishes in reverse order.
            await new Promise(resolve => setTimeout(resolve, (fixture.bytes.length - first) * 5));
            --active;
            ++finished;
        }
        return new Response(bytes.slice(first, last + 1).buffer);
    };
    const parser = await import('./mega.js');
    const definitions = parser.default.metadata.options;
    for (const [name, expected] of [['maxConnections', 10], ['maxChunkSize', 4 * 1024 * 1024]]) {
        const definition = definitions.find(option => option.name === name);
        if (!definition || definition.default !== expected || definition.minimum !== 1 || !definition.integer)
            throw new Error('Missing or invalid MEGA transfer option: ' + name);
    }
    const options = input.url.includes('invalidConnections') ? {maxConnections: 0}
        : input.url.includes('invalidChunkSize') ? {maxChunkSize: 1.5}
        : input.url.includes('configured') ? {maxConnections: 4, maxChunkSize: 8 * 1024 * 1024}
        : input.url.includes('reordered') ? {maxConnections: 3, maxChunkSize: 4} : {};
    await parser.run({url, domain: 'https://mega.nz', operation: 'shared', params: {url}, options}, host);
    if (input.url.includes('reordered')) {
        const refilled = Math.max(...transfers.filter(transfer => transfer.afterFirstFinish).map(transfer => transfer.active));
        if (transfers.length !== Math.ceil(fixture.bytes.length / 4)) throw new Error('Unexpected chunk count: ' + transfers.length);
        if (refilled !== 3) throw new Error('Connections after the first finished chunk peaked at ' + refilled + ' of 3');
    }
}
)JS" );
    harness.configure( R"([{"pattern":"/.*","export":"run"}])" );
    const std::string path { retry ? "/retry" : selected ? "/selected" : folder ? "/folder" : corrupt ? "/corrupt" : "/valid" };
	const std::string query {
		invalid_connections ? "?invalidConnections" :
		invalid_chunk_size  ? "?invalidChunkSize" :
		configured          ? "?configured" :
		reordered           ? "?reordered" :
							  ""
	};
	auto session { harness.session( harness.server.url( path + query ) ) };
    REQUIRE( session->submit( session->rootUrl() ).has_value() );
    REQUIRE( waitUntil( [&] { return session->idle(); } ) );
	folly::coro::blockingWait( session->wait() );
	if ( repeated )
	{
		const auto next { harness.session( session->rootUrl() ) };
		REQUIRE( next->submit( next->rootUrl() ).has_value() );
		REQUIRE( waitUntil( [ & ] { return next->idle(); } ) );
		folly::coro::blockingWait( next->wait() );
	}
	folly::coro::blockingWait( harness.context->shutdown() );
	for ( const auto& failure : harness.observer.failures ) INFO( failure.second );
    if ( corrupt || invalid_connections || invalid_chunk_size )
    {
        REQUIRE( harness.imports.stored.empty() );
        REQUIRE_FALSE( harness.observer.failures.empty() );
    }
    else
    {
        REQUIRE( harness.observer.failures.empty() );
		REQUIRE( harness.imports.stored.size() == ( repeated ? 2 : 1 ) );
		INFO( "Stored bytes: " << harness.imports.stored[0].second );
        REQUIRE( harness.imports.stored[0].second == "MEGA direct import fixture\n" );
        REQUIRE( harness.imports.metadata[0].filename == "mega-test.bin" );
		if ( repeated ) REQUIRE( harness.imports.stored[ 1 ].second == "MEGA direct import fixture\n" );
	}
}
} // namespace idhan::test
