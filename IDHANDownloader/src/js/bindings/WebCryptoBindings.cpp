#include "js/bindings/WebCryptoBindings.hpp"

#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

#include "js/JSValueHandle.hpp"
#include "js/QuickJSBinding.hpp"
#include "js/bindings/IdhanBindings.hpp"

namespace idhan::downloader::webcrypto
{
using Bytes = std::vector< unsigned char >;
using CipherContext = std::unique_ptr< EVP_CIPHER_CTX, decltype( &EVP_CIPHER_CTX_free ) >;
using BigNumber = std::unique_ptr< BIGNUM, decltype( &BN_free ) >;

static std::span< unsigned char > buffer( JSContext* context, JSValueConst value )
{
	std::size_t size {};
	auto* bytes { JS_GetArrayBuffer( context, &size, value ) };
	if ( bytes == nullptr ) throw std::runtime_error( "Expected an attached ArrayBuffer" );

	if ( size > static_cast< std::size_t >( std::numeric_limits< int >::max() - EVP_MAX_BLOCK_LENGTH ) )
		throw std::runtime_error( "Crypto input is too large" );

	return { bytes, size };
}

static void requireSuccess( const int result )
{
	if ( result != 1 ) throw std::runtime_error( "Native cryptographic operation failed" );
}

static Bytes digest( const std::string& name, const std::span< const unsigned char > input )
{
	if ( name != "SHA-1" && name != "SHA-256" && name != "SHA-384" && name != "SHA-512" )
		throw std::runtime_error( "Unsupported digest algorithm" );

	const auto* algorithm { EVP_get_digestbyname( name.c_str() ) };
	if ( algorithm == nullptr ) throw std::runtime_error( "Digest algorithm is unavailable" );

	Bytes output( EVP_MAX_MD_SIZE );
	unsigned int size {};
	requireSuccess( EVP_Digest( input.data(), input.size(), output.data(), &size, algorithm, nullptr ) );
	output.resize( size );
	return output;
}

//! OpenSSL's CTR mode increments all 128 bits. WebCrypto increments only the low
//! `length` bits, so split at that field's wrap boundary and reset that field.
static Bytes ctr(
	const EVP_CIPHER* cipher,
	const std::span< const unsigned char > key,
	const std::span< const unsigned char > input,
	const std::span< const unsigned char > iv,
	const int length )
{
	if ( iv.size() != 16 ) throw std::runtime_error( "AES-CTR requires a 16-byte counter" );

	if ( length < 1 || length > 128 ) throw std::runtime_error( "Invalid AES-CTR counter length" );

	const auto blocks { ( input.size() + 15 ) / 16 };
	BigNumber modulus { BN_new(), BN_free };
	BigNumber value { BN_bin2bn( iv.data(), 16, nullptr ), BN_free };
	BigNumber remaining { BN_new(), BN_free };
	if ( !modulus || !value || !remaining ) throw std::bad_alloc {};

	requireSuccess( BN_set_bit( modulus.get(), length ) );
	if ( BN_num_bits( value.get() ) > length ) requireSuccess( BN_mask_bits( value.get(), length ) );

	requireSuccess( BN_sub( remaining.get(), modulus.get(), value.get() ) );
	if ( BN_num_bits( modulus.get() ) <= 32 && blocks > BN_get_word( modulus.get() ) )
		throw std::runtime_error( "AES-CTR input would repeat a counter" );

	const auto first_blocks {
		BN_num_bits( remaining.get() ) > 32 ? blocks : std::min< std::size_t >( blocks, BN_get_word( remaining.get() ) )
	};
	const auto first_size { std::min( input.size(), first_blocks * 16 ) };
	std::array< unsigned char, 16 > counter {};
	std::copy( iv.begin(), iv.end(), counter.begin() );
	Bytes output( input.size() + 16 );
	CipherContext context { EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free };
	if ( !context ) throw std::bad_alloc {};

	requireSuccess( EVP_EncryptInit_ex( context.get(), cipher, nullptr, key.data(), counter.data() ) );
	int written {};
	requireSuccess(
		EVP_EncryptUpdate( context.get(), output.data(), &written, input.data(), static_cast< int >( first_size ) ) );
	if ( first_size < input.size() )
	{
		for ( int bit {}; bit < length; ++bit )
		{
			const auto index { static_cast< std::size_t >( 15 - bit / 8 ) };
			counter[ index ] &= static_cast< unsigned char >( ~( 1u << ( bit % 8 ) ) );
		}
		requireSuccess( EVP_EncryptInit_ex( context.get(), cipher, nullptr, key.data(), counter.data() ) );
		int tail {};
		requireSuccess( EVP_EncryptUpdate(
			context.get(),
			output.data() + written,
			&tail,
			input.data() + first_size,
			static_cast< int >( input.size() - first_size ) ) );
		written += tail;
	}

	int final_size {};
	requireSuccess( EVP_EncryptFinal_ex( context.get(), output.data() + written, &final_size ) );
	output.resize( static_cast< std::size_t >( written + final_size ) );
	return output;
}

static Bytes aes(
	const bool encrypt,
	const std::string& mode,
	const std::span< const unsigned char > key,
	const std::span< const unsigned char > input,
	const std::span< const unsigned char > iv,
	const std::span< const unsigned char > additional,
	const int parameter )
{
	if ( key.size() != 16 && key.size() != 24 && key.size() != 32 )
		throw std::runtime_error( "Invalid AES key length" );

	if ( mode != "AES-CTR" && mode != "AES-CBC" && mode != "AES-GCM" )
		throw std::runtime_error( "Unsupported cipher algorithm" );

	const std::string suffix { mode == "AES-CTR" ? "ctr" : mode == "AES-CBC" ? "cbc" : "gcm" };
	const std::string name { "aes-" + std::to_string( key.size() * 8 ) + "-" + suffix };
	const auto* cipher { EVP_get_cipherbyname( name.c_str() ) };
	if ( cipher == nullptr ) throw std::runtime_error( "AES algorithm is unavailable" );

	if ( mode == "AES-CTR" ) return ctr( cipher, key, input, iv, parameter );

	const bool gcm { mode == "AES-GCM" };
	if ( !gcm && iv.size() != 16 ) throw std::runtime_error( "AES-CBC requires a 16-byte IV" );

	if ( gcm && iv.empty() ) throw std::runtime_error( "AES-GCM requires a nonempty IV" );

	if ( gcm && parameter != 32 && parameter != 64 && parameter != 96 && parameter != 104 && parameter != 112
	     && parameter != 120 && parameter != 128 )
		throw std::runtime_error( "Invalid GCM tag length" );

	const auto tag_size { static_cast< std::size_t >( gcm ? parameter / 8 : 0 ) };
	if ( !encrypt && input.size() < tag_size ) throw std::runtime_error( "Missing GCM authentication tag" );

	const auto data_size { input.size() - ( encrypt ? 0 : tag_size ) };
	CipherContext context { EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free };
	if ( !context ) throw std::bad_alloc {};

	requireSuccess( EVP_CipherInit_ex( context.get(), cipher, nullptr, nullptr, nullptr, encrypt ) );
	if ( gcm )
		requireSuccess(
			EVP_CIPHER_CTX_ctrl( context.get(), EVP_CTRL_GCM_SET_IVLEN, static_cast< int >( iv.size() ), nullptr ) );

	requireSuccess( EVP_CipherInit_ex( context.get(), nullptr, nullptr, key.data(), iv.data(), encrypt ) );
	int written {};
	if ( gcm && !additional.empty() )
		requireSuccess( EVP_CipherUpdate(
			context.get(), nullptr, &written, additional.data(), static_cast< int >( additional.size() ) ) );

	Bytes output( input.size() + EVP_MAX_BLOCK_LENGTH + tag_size );
	requireSuccess(
		EVP_CipherUpdate( context.get(), output.data(), &written, input.data(), static_cast< int >( data_size ) ) );
	if ( gcm && !encrypt )
		requireSuccess( EVP_CIPHER_CTX_ctrl(
			context.get(),
			EVP_CTRL_GCM_SET_TAG,
			static_cast< int >( tag_size ),
			const_cast< unsigned char* >( input.data() + data_size ) ) );

	int final_size {};
	requireSuccess( EVP_CipherFinal_ex( context.get(), output.data() + written, &final_size ) );
	output.resize( static_cast< std::size_t >( written + final_size ) + ( encrypt ? tag_size : 0 ) );
	if ( gcm && encrypt )
		requireSuccess( EVP_CIPHER_CTX_ctrl(
			context.get(),
			EVP_CTRL_GCM_GET_TAG,
			static_cast< int >( tag_size ),
			output.data() + written + final_size ) );

	return output;
}

static IDHAN_QJS_FUNCTION( nativeCrypto )
{
	(void)this_value;
	try
	{
		if ( argument_count < 3 ) return JS_ThrowTypeError( context, "Missing crypto arguments" );

		const auto operation { scriptValueString( context, arguments[ 0 ] ) };
		const auto algorithm { scriptValueString( context, arguments[ 1 ] ) };
		auto input { buffer( context, arguments[ 2 ] ) };
		Bytes output;
		if ( operation == "random" )
		{
			if ( input.size() > 65536 ) throw std::runtime_error( "Random quota exceeded" );

			requireSuccess( RAND_bytes( input.data(), static_cast< int >( input.size() ) ) );
			return JS_UNDEFINED;
		}

		if ( operation == "digest" )
			output = digest( algorithm, input );
		else
		{
			if ( argument_count < 7 ) return JS_ThrowTypeError( context, "Missing AES arguments" );

			if ( operation != "encrypt" && operation != "decrypt" )
				return JS_ThrowTypeError( context, "Unknown crypto operation" );

			const auto key { buffer( context, arguments[ 3 ] ) };
			const auto iv { buffer( context, arguments[ 4 ] ) };
			const auto additional { buffer( context, arguments[ 5 ] ) };
			std::int32_t parameter {};
			if ( JS_ToInt32( context, &parameter, arguments[ 6 ] ) < 0 ) return JS_EXCEPTION;

			output = aes( operation == "encrypt", algorithm, key, input, iv, additional, parameter );
		}

		return JS_NewArrayBufferCopy( context, output.data(), output.size() );
	}
	IDHAN_QJS_TRANSLATE_EXCEPTIONS( context )
}

constexpr std::string_view bootstrap { R"JS(
(function(native) {
    'use strict';
    const records = new WeakMap();
    const token = {};
    const aesNames = ['AES-CTR', 'AES-CBC', 'AES-GCM'];
    const hashes = ['SHA-1', 'SHA-256', 'SHA-384', 'SHA-512'];
    function error(name, message) {
        const result = new Error(message);
        result.name = name;
        return result;
    }
    function bytes(value) {
        if (value instanceof ArrayBuffer) return new Uint8Array(value).slice();
        if (ArrayBuffer.isView(value)) return new Uint8Array(value.buffer, value.byteOffset, value.byteLength).slice();
        throw new TypeError('Expected an ArrayBuffer or ArrayBufferView');
    }
    function algorithmName(algorithm) {
        const name = typeof algorithm === 'string' ? algorithm : algorithm?.name;
        if (typeof name !== 'string') throw new TypeError('Algorithm name is required');
        return name.toUpperCase();
    }
    function aesName(algorithm) {
        const name = algorithmName(algorithm);
        if (!aesNames.includes(name)) throw error('NotSupportedError', 'Unsupported algorithm: ' + name);
        return name;
    }
    function usagesList(usages) {
        const result = Array.from(usages);
        if (result.some(usage => !['encrypt', 'decrypt', 'wrapKey', 'unwrapKey'].includes(usage)))
            throw error('SyntaxError', 'Invalid AES key usage');
        if (result.length === 0) throw error('SyntaxError', 'A secret key must have usages');
        return [...new Set(result)];
    }
    function invoke(...args) {
        try { return native(...args); }
        catch (cause) { throw error('OperationError', cause.message); }
    }
    class CryptoKey {
        constructor(secret, data) {
            if (secret !== token) throw new TypeError('Illegal constructor');
            records.set(this, data);
        }
        get type() { return keyRecord(this).type; }
        get extractable() { return keyRecord(this).extractable; }
        get algorithm() { return {...keyRecord(this).algorithm}; }
        get usages() { return [...keyRecord(this).usages]; }
        get [Symbol.toStringTag]() { return 'CryptoKey'; }
    }
    function keyRecord(key, name, usage) {
        const record = records.get(key);
        if (!record) throw new TypeError('Expected a CryptoKey');
        if (name !== undefined && (record.algorithm.name !== name || !record.usages.includes(usage)))
            throw error('InvalidAccessError', 'Key algorithm or usage does not permit this operation');
        return record;
    }
    async function importKey(format, keyData, algorithm, extractable, usages) {
        if (format !== 'raw') throw error('NotSupportedError', 'Only raw AES key import is supported');
        const name = aesName(algorithm);
        const raw = bytes(keyData);
        if (![16, 24, 32].includes(raw.length)) throw error('DataError', 'AES key must be 128, 192, or 256 bits');
        return new CryptoKey(token, {type: 'secret', raw, algorithm: {name, length: raw.length * 8},
            extractable: Boolean(extractable), usages: usagesList(usages)});
    }
    async function exportKey(format, key) {
        const record = keyRecord(key);
        if (!record.extractable) throw error('InvalidAccessError', 'Key is not extractable');
        if (format !== 'raw') throw error('NotSupportedError', 'Only raw AES key export is supported');
        return record.raw.slice().buffer;
    }
    async function generateKey(algorithm, extractable, usages) {
        const name = aesName(algorithm);
        if (![128, 192, 256].includes(algorithm.length)) throw error('OperationError', 'Invalid AES key length');
        return importKey('raw', getRandomValues(new Uint8Array(algorithm.length / 8)), name, extractable, usages);
    }
    async function digest(algorithm, data) {
        const name = algorithmName(algorithm);
        if (!hashes.includes(name)) throw error('NotSupportedError', 'Unsupported digest: ' + name);
        return invoke('digest', name, bytes(data).buffer);
    }
    async function cipher(operation, algorithm, key, data) {
        const name = aesName(algorithm);
        const record = keyRecord(key, name, operation);
        const input = bytes(data);
        const iv = bytes(name === 'AES-CTR' ? algorithm.counter : algorithm.iv);
        let parameter = 0;
        if (name === 'AES-CTR') {
            parameter = algorithm.length;
            if (!Number.isInteger(parameter) || parameter < 1 || parameter > 128)
                throw error('OperationError', 'AES-CTR length must be between 1 and 128');
        }
        if (name === 'AES-GCM') {
            parameter = algorithm.tagLength === undefined ? 128 : algorithm.tagLength;
            if (![32, 64, 96, 104, 112, 120, 128].includes(parameter))
                throw error('OperationError', 'Invalid AES-GCM tag length');
        }
        const additional = name !== 'AES-GCM' || algorithm.additionalData === undefined
            ? new Uint8Array() : bytes(algorithm.additionalData);
        return invoke(operation, name, input.buffer, record.raw.buffer, iv.buffer, additional.buffer, parameter);
    }
    function getRandomValues(view) {
        const kinds = ['Int8Array', 'Uint8Array', 'Uint8ClampedArray', 'Int16Array', 'Uint16Array',
            'Int32Array', 'Uint32Array', 'BigInt64Array', 'BigUint64Array'];
        const kind = Object.prototype.toString.call(view).slice(8, -1);
        if (!ArrayBuffer.isView(view) || !kinds.includes(kind))
            throw error('TypeMismatchError', 'Expected an integer typed array');
        if (view.byteLength > 65536) throw error('QuotaExceededError', 'Random data exceeds 65536 bytes');
        const output = new Uint8Array(view.byteLength);
        invoke('random', '', output.buffer);
        new Uint8Array(view.buffer, view.byteOffset, view.byteLength).set(output);
        return view;
    }
    function randomUUID() {
        const data = getRandomValues(new Uint8Array(16));
        data[6] = (data[6] & 15) | 64;
        data[8] = (data[8] & 63) | 128;
        const hex = Array.from(data, value => value.toString(16).padStart(2, '0')).join('');
        return `${hex.slice(0,8)}-${hex.slice(8,12)}-${hex.slice(12,16)}-${hex.slice(16,20)}-${hex.slice(20)}`;
    }
    async function unsupported() { throw error('NotSupportedError', 'This WebCrypto operation is not implemented'); }
    const subtle = Object.freeze({digest, importKey, exportKey, generateKey,
        encrypt: (algorithm, key, data) => cipher('encrypt', algorithm, key, data),
        decrypt: (algorithm, key, data) => cipher('decrypt', algorithm, key, data),
        sign: unsupported, verify: unsupported, deriveBits: unsupported, deriveKey: unsupported,
        wrapKey: unsupported, unwrapKey: unsupported});
    Object.defineProperty(globalThis, 'CryptoKey', {value: CryptoKey, configurable: true});
    Object.defineProperty(globalThis, 'crypto', {value: Object.freeze({subtle, getRandomValues, randomUUID}), configurable: true});
})
)JS" };
} // namespace idhan::downloader::webcrypto

namespace idhan::downloader
{
std::expected< void, std::string > installWebCryptoBindings( JSContext* context )
{
	JSValueHandle factory {
		context,
		JS_Eval(
			context, webcrypto::bootstrap.data(), webcrypto::bootstrap.size(), "webcrypto.js", JS_EVAL_TYPE_GLOBAL )
	};
	if ( JS_IsException( factory.get() ) ) return std::unexpected( scriptExceptionString( context ) );

	JSValueHandle native { context, JS_NewCFunction( context, webcrypto::nativeCrypto, "crypto", 7 ) };
	JSValue argument { native.get() };
	JSValueHandle installed { context, JS_Call( context, factory.get(), JS_UNDEFINED, 1, &argument ) };
	if ( JS_IsException( installed.get() ) ) return std::unexpected( scriptExceptionString( context ) );

	return {};
}
} // namespace idhan::downloader
