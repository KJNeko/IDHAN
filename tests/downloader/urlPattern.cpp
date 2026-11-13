#include <catch2/catch_test_macros.hpp>

#include "harness/Fixtures.hpp"
#include "js/ScriptContext.hpp"
#include "js/ScriptExecution.hpp"
#include "js/JSValueHandle.hpp"
#include "js/bindings/IdhanBindings.hpp"

namespace idhan::test
{

TEST_CASE( "Native URLPattern accepts locale and repeated pathname captures", "[downloader][url-pattern]" )
{
	ParserFixture files;
	downloader::BytecodeCache cache { files.root() };
	downloader::ScriptContext scripts { {}, cache };
	downloader::ScriptExecution execution;
	const auto realm_e { scripts.createRealm( execution ) };
	REQUIRE( realm_e.has_value() );
	downloader::JSContextPtr realm { realm_e.value() };
	execution.context = realm.get();
	const std::string source { R"JS(
const localized = new URLPattern({ pathname: '/:locale([a-z]{2})/artworks/:id' });
if (!localized.test('https://www.pixiv.net/en/artworks/123')) throw new Error('Locale did not match');
const original = new URLPattern({ pathname: '/img-original/:path*' });
const match = original.exec('https://i.pximg.net/img-original/img/2020/01/01/image.png');
if (match?.pathname.groups.path !== 'img/2020/01/01/image.png') throw new Error('Repeated path did not match');
)JS" };
	downloader::JSValueHandle result { realm.get(), JS_Eval(
		realm.get(), source.c_str(), source.size(), "url-pattern.js", JS_EVAL_TYPE_GLOBAL ) };
	const bool failed { JS_IsException( result.get() ) != 0 };
	INFO( ( failed ? downloader::scriptExceptionString( realm.get() ) : "" ) );
	CHECK_FALSE( failed );
}

} // namespace idhan::test
