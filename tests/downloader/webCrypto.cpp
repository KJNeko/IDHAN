#include <catch2/catch_test_macros.hpp>

#include <fstream>

#include "harness/Fixtures.hpp"
#include "js/JSValueHandle.hpp"
#include "js/ScriptContext.hpp"
#include "js/ScriptExecution.hpp"
#include "js/bindings/IdhanBindings.hpp"

namespace idhan::test
{
TEST_CASE( "Parser WebCrypto provides native AES, digests and secure randomness", "[downloader][webcrypto]" )
{
	ParserFixture files;
	downloader::BytecodeCache cache { files.root() };
	downloader::ScriptContext scripts { {}, cache };
	downloader::ScriptExecution execution;
	const auto realm_e { scripts.createRealm( execution ) };
	REQUIRE( realm_e.has_value() );
	downloader::JSContextPtr realm { realm_e.value() };
	execution.context = realm.get();
	// Cross-implementation vectors generated with Node's WebCrypto, including CTR field rollover.
	std::ifstream input {
		std::filesystem::path { IDHAN_SOURCE_DIR } / "tests/downloader/fixtures/webcrypto-vectors.json"
	};
	const std::string vectors { std::istreambuf_iterator< char > { input }, std::istreambuf_iterator< char > {} };
	REQUIRE_FALSE( vectors.empty() );
	const std::string source { "const vectors = " + vectors + R"JS(;
(async function() {
    function check(value, message) { if (!value) throw new Error(message); }
    function hex(value) { return Array.from(new Uint8Array(value), b => b.toString(16).padStart(2, '0')).join(''); }
    // Matches hexadecimal byte pairs, e.g. "ab" and "01" in "ab01".
    function bytes(value) { return Uint8Array.from(value.match(/[0-9a-f]{2}/g) ?? [], b => parseInt(b, 16)); }
    async function rejects(operation, name) {
        try { await operation(); } catch (error) { check(error.name === name, `${error.name} != ${name}`); return; }
        throw new Error('Expected rejection: ' + name);
    }
    for (const vector of vectors) {
        const keyBytes = bytes('aa' + vector.key + 'bb');
        const view = keyBytes.subarray(1, keyBytes.length - 1);
        const key = await crypto.subtle.importKey('raw', view, vector.name.toLowerCase(), true, ['encrypt', 'decrypt']);
        check(key instanceof CryptoKey && key.type === 'secret', 'Key branding');
        check(hex(await crypto.subtle.exportKey('raw', key)) === vector.key, 'Key view boundaries');
        key.algorithm.name = 'broken';
        key.usages.length = 0;
        view.fill(0);
        const algorithm = vector.name === 'AES-CTR'
            ? {name: vector.name, counter: bytes(vector.iv), length: vector.length}
            : {name: vector.name, iv: bytes(vector.iv)};
        const encrypted = await crypto.subtle.encrypt(algorithm, key, bytes(vector.input));
        check(hex(encrypted) === vector.output, 'Encryption vector: ' + vector.name + '/' + vector.length);
        check(hex(await crypto.subtle.decrypt(algorithm, key, encrypted)) === vector.input, 'Decryption vector');
    }
    const ctrKey = await crypto.subtle.generateKey({name: 'AES-CTR', length: 256}, false, ['decrypt']);
    check(ctrKey.algorithm.length === 256 && !ctrKey.extractable, 'Generated key metadata');
    await rejects(() => crypto.subtle.exportKey('raw', ctrKey), 'InvalidAccessError');
    const ctr = {name: 'AES-CTR', counter: new Uint8Array(16), length: 1};
    await rejects(() => crypto.subtle.encrypt(ctr, ctrKey, new Uint8Array(1)), 'InvalidAccessError');
    await rejects(() => crypto.subtle.decrypt(ctr, ctrKey, new Uint8Array(33)), 'OperationError');
    await rejects(() => crypto.subtle.decrypt({...ctr, counter: new Uint8Array(15)}, ctrKey, new Uint8Array(1)), 'OperationError');
    await rejects(() => crypto.subtle.importKey('raw', new Uint8Array(15), 'AES-CBC', false, ['encrypt']), 'DataError');
    await rejects(() => crypto.subtle.importKey('raw', new Uint8Array(16), 'AES-CBC', false, ['sign']), 'SyntaxError');
    await rejects(() => crypto.subtle.importKey('raw', new Uint8Array(16), 'AES-CBC', false, []), 'SyntaxError');
    await rejects(() => crypto.subtle.digest('MD5', new Uint8Array()), 'NotSupportedError');
    await rejects(() => crypto.subtle.sign(), 'NotSupportedError');
    const gcmKey = await crypto.subtle.generateKey({name: 'AES-GCM', length: 128}, false, ['encrypt', 'decrypt']);
    const gcm = {name: 'AES-GCM', iv: new Uint8Array(12), additionalData: new Uint8Array([1, 2]), tagLength: 96};
    const authenticated = new Uint8Array(await crypto.subtle.encrypt(gcm, gcmKey, new Uint8Array([42])));
    authenticated[0] ^= 1;
    await rejects(() => crypto.subtle.decrypt(gcm, gcmKey, authenticated), 'OperationError');
    const cbcKey = await crypto.subtle.generateKey({name: 'AES-CBC', length: 128}, false, ['decrypt']);
    await rejects(() => crypto.subtle.decrypt({name: 'AES-CBC', iv: new Uint8Array(16)}, cbcKey, new Uint8Array(3)), 'OperationError');
    check(hex(await crypto.subtle.digest('SHA-256', new Uint8Array([97,98,99]))) ===
        'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad', 'SHA-256 known answer');
    check(hex(await crypto.subtle.digest('SHA-1', new Uint8Array())) ===
        'da39a3ee5e6b4b0d3255bfef95601890afd80709', 'Empty SHA-1 known answer');
    for (const [name, size] of [['SHA-384', 48], ['SHA-512', 64]])
        check((await crypto.subtle.digest(name, new Uint8Array())).byteLength === size, 'Digest length');
    const largeKey = await crypto.subtle.generateKey({name: 'AES-CTR', length: 128}, false, ['encrypt', 'decrypt']);
    const largeData = new Uint8Array(16 * 1024 * 1024);
    largeData[0] = 42;
    largeData[largeData.length - 1] = 99;
    const largeAlgorithm = {name: 'AES-CTR', counter: new Uint8Array(16), length: 64};
    const largeEncrypted = await crypto.subtle.encrypt(largeAlgorithm, largeKey, largeData);
    const largeDecrypted = new Uint8Array(await crypto.subtle.decrypt(largeAlgorithm, largeKey, largeEncrypted));
    check(largeDecrypted[0] === 42 && largeDecrypted[largeDecrypted.length - 1] === 99, '16 MiB native crypto burst');
    check(crypto.getRandomValues(new Uint8Array()).length === 0, 'Empty random buffer');
    const random = new Uint8Array(34).fill(99);
    check(crypto.getRandomValues(random.subarray(1, 33)).length === 32, 'Random return value');
    check(random[0] === 99 && random[33] === 99 && random.some(b => b !== 99), 'Random view boundaries');
    await rejects(() => crypto.getRandomValues(new Uint8Array(65537)), 'QuotaExceededError');
    await rejects(() => crypto.getRandomValues(new Float32Array(1)), 'TypeMismatchError');
    // Matches a lowercase RFC 4122 version 4 UUID, e.g. 12345678-1234-4abc-8def-123456789abc.
    check(/^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/.test(crypto.randomUUID()), 'UUID shape');
    return 'passed';
})()
)JS" };
	downloader::JSValueHandle result {
		realm.get(), JS_Eval( realm.get(), source.data(), source.size(), "webcrypto-test.js", JS_EVAL_TYPE_GLOBAL )
	};
	REQUIRE_FALSE( JS_IsException( result.get() ) );
	std::string error;
	REQUIRE( scripts.runReadyMicrotasks( error ) );
	downloader::JSValueHandle settled { realm.get(), JS_PromiseResult( realm.get(), result.get() ) };
	INFO( downloader::scriptErrorString( realm.get(), settled.get() ) );
	CHECK( JS_PromiseState( realm.get(), result.get() ) == JS_PROMISE_FULFILLED );
	CHECK( downloader::scriptValueString( realm.get(), settled.get() ) == "passed" );
}
} // namespace idhan::test
