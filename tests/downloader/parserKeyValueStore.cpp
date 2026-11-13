#include <catch2/catch_test_macros.hpp>

#include "harness/Fixtures.hpp"
#include "js/JSValueHandle.hpp"
#include "js/ScriptContext.hpp"
#include "js/ScriptExecution.hpp"
#include "js/bindings/IdhanBindings.hpp"
#include "scripts/ParserKeyValueStore.hpp"

namespace idhan::test
{

TEST_CASE( "Parser KV reports hits without extending cache lifetimes", "[downloader][parser-kv]" )
{
	using namespace std::chrono_literals;
	downloader::ParserKeyValueStore store;
	const auto start { downloader::ParserKeyValueStore::Clock::now() };
	REQUIRE( store.snapshot( 100, start ).hit_rate == 0 );
	store.set( "b", "hot", "1", 10s, start );
	store.set( "a", "hot", "1", 10s, start );
	store.set( "a", "cold", "1", 10s, start );
	REQUIRE( store.get( "b", "hot", start + 1s ) == "1" );
	REQUIRE( store.get( "b", "hot", start + 2s ) == "1" );
	REQUIRE( store.get( "a", "hot", start + 1s ) == "1" );
	REQUIRE( store.get( "a", "hot", start + 2s ) == "1" );
	REQUIRE_FALSE( store.get( "a", "missing", start + 2s ).has_value() );
	// Replacing a live value retains its hits; writes do not count as reads.
	store.set( "a", "hot", "2", 10s, start + 2s );
	const auto snapshot { store.snapshot( 2, start + 5s ) };
	REQUIRE( snapshot.hits == 4 );
	REQUIRE( snapshot.misses == 1 );
	REQUIRE( snapshot.hit_rate == 0.8 );
	REQUIRE( snapshot.entry_count == 3 );
	REQUIRE( snapshot.entries.size() == 2 );
	REQUIRE( snapshot.entries[ 0 ].parser == "a" );
	REQUIRE( snapshot.entries[ 0 ].key == "hot" );
	REQUIRE( snapshot.entries[ 0 ].hits == 2 );
	REQUIRE( snapshot.entries[ 0 ].lifetime_seconds == 10 );
	REQUIRE( snapshot.entries[ 0 ].expires_in_seconds == 7 );
	REQUIRE( snapshot.entries[ 1 ].parser == "b" );
	REQUIRE( store.snapshot( 100, start + 7s ).entries[ 0 ].expires_in_seconds == 5 );
	REQUIRE( store.snapshot( 100, start + 12s ).entries.empty() );
	REQUIRE_FALSE( store.get( "a", "hot", start + 12s ).has_value() );
	const auto expired { store.snapshot( 100, start + 12s ) };
	REQUIRE( expired.hits == 4 );
	REQUIRE( expired.misses == 2 );
	REQUIRE( expired.entry_count == 0 );
}

TEST_CASE( "Parser KV uses sliding expiration and isolates parsers", "[downloader][parser-kv]" )
{
	using namespace std::chrono_literals;
	downloader::ParserKeyValueStore store;
	const auto start { downloader::ParserKeyValueStore::Clock::now() };
	store.set( "parser", "tag", "1", 24h, start );
	REQUIRE( store.get( "other", "tag", start ) == std::nullopt );
	REQUIRE( store.get( "parser", "tag", start + 23h ) == "1" );
	REQUIRE( store.get( "parser", "tag", start + 46h ) == "1" );
	REQUIRE( store.get( "parser", "tag", start + 70h ) == std::nullopt );

	store.set( "parser", "custom", "null", 10s, start );
	REQUIRE( store.get( "parser", "custom", start + 9s ) == "null" );
	REQUIRE( store.get( "parser", "custom", start + 18s ) == "null" );
	REQUIRE( store.get( "parser", "custom", start + 28s ) == std::nullopt );

	store.set( "parser", "default", "0" );
	REQUIRE( store.get( "parser", "default", start + 23h ) == "0" );
	REQUIRE( store.get( "parser", "default", start + 48h ) == std::nullopt );
	store.set( "parser", "delete", "false" );
	REQUIRE( store.erase( "parser", "delete" ) );
	REQUIRE_FALSE( store.erase( "parser", "delete" ) );
	REQUIRE_THROWS_AS( store.set( "parser", "bad", "1", 0s ), std::invalid_argument );
}

TEST_CASE( "Parser KV bindings share JSON copies across runtimes", "[downloader][parser-kv]" )
{
	ParserFixture files;
	downloader::BytecodeCache cache { files.root() };
	const auto store { std::make_shared< downloader::ParserKeyValueStore >() };
	downloader::ScriptContext first { {}, cache, {}, store };
	downloader::ScriptContext second { {}, cache, {}, store };
	downloader::ScriptExecution execution;

	for ( auto* scripts : { &first, &second } )
	{
		const auto realm_e { scripts->createRealm( execution ) };
		REQUIRE( realm_e.has_value() );
		downloader::JSContextPtr realm { realm_e.value() };
		const std::string source { scripts == &first ? R"JS(
const original = {tags: ["cat"]};
idhan.kv("set", "parser", "tags", original);
original.tags.push("dog");
idhan.kv("set", "parser", "custom-seconds", 42, 60);
true;
)JS" : R"JS(
function check(value) { if (!value) throw new Error("KV assertion failed"); }
const value = idhan.kv("get", "parser", "tags");
check(value.tags.join(",") === "cat");
value.tags.push("dog");
check(idhan.kv("get", "parser", "tags").tags.length === 1);
check(idhan.kv("get", "other", "tags") === undefined);
for (const invalid of [0, -1, 0.5, 1.5, NaN, Infinity, "100", 1e30]) {
    let rejected = false;
    try { idhan.kv("set", "parser", "bad", 1, invalid); }
    catch { rejected = true; }
    check(rejected);
}
check(idhan.kv("delete", "parser", "tags"));
check(idhan.kv("get", "parser", "tags") === undefined);
true;
)JS" };
		downloader::JSValueHandle result {
			realm.get(), JS_Eval( realm.get(), source.data(), source.size(), "kv-test.js", JS_EVAL_TYPE_GLOBAL )
		};
		if ( JS_IsException( result.get() ) ) FAIL( downloader::scriptExceptionString( realm.get() ) );

		REQUIRE( JS_ToBool( realm.get(), result.get() ) == 1 );
	}

	const auto now { downloader::ParserKeyValueStore::Clock::now() };
	REQUIRE( store->get( "parser", "custom-seconds", now + std::chrono::seconds { 59 } ) == "42" );
	REQUIRE( store->get( "parser", "custom-seconds", now + std::chrono::seconds { 120 } ) == std::nullopt );
}

} // namespace idhan::test
