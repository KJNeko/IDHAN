#pragma once

#include <openssl/evp.h>
#include <openssl/sha.h>

#include <array>
#include <cstring>
#include <memory>

#include "decodeHex.hpp"
#include "fgl/defines.hpp"
#include "logging/format_ns.hpp"

namespace idhan::crypto
{

inline std::array< std::byte, 256 / 8 > hashData( const std::byte* data, const std::size_t size )
{
	const std::unique_ptr< EVP_MD_CTX, void ( * )( EVP_MD_CTX* ) > ctx {
		EVP_MD_CTX_new(), []( EVP_MD_CTX* ptr ) { EVP_MD_CTX_free( ptr ); }
	};
	if ( !ctx ) throw std::runtime_error( "EVP_MD_CTX_new() failed" );

	if ( EVP_DigestInit_ex( ctx.get(), EVP_sha256(), nullptr ) != 1 )
		throw std::runtime_error( "EVP_DigestInit_ex() failed" );

	if ( EVP_DigestUpdate( ctx.get(), data, size ) != 1 ) throw std::runtime_error( "EVP_DigestUpdate() failed" );

	unsigned char hash[ EVP_MAX_MD_SIZE ];
	unsigned int hash_length { 0 };

	if ( EVP_DigestFinal_ex( ctx.get(), hash, &hash_length ) != 1 )
		throw std::runtime_error( "EVP_DigestFinal_ex() failed" );

	std::array< std::byte, 256 / 8 > out_data {};

	FGL_ASSERT( out_data.size() == static_cast< std::size_t >( hash_length ), "Invalid size" );

	std::memcpy( out_data.data(), hash, hash_length );

	return out_data;
}

//! Parses a 64-character hex string into a 32-byte SHA-256 value.
//! \throws std::runtime_error if the decoded length is not exactly 32 bytes.
//! \throws std::invalid_argument if \p string is not valid hex (via decodeHex).
inline std::array< std::byte, 256 / 8 > fromHex( const std::string& string )
{
	std::array< std::byte, 256 / 8 > hash {};
	const auto hash_decoded { decodeHex( string ) };
	if ( hash_decoded.size() != hash.size() ) throw std::runtime_error( "Invalid size" );
	std::memcpy( hash.data(), hash_decoded.data(), hash.size() );
	return hash;
}

//! Formats a 32-byte SHA-256 value as a lowercase 64-character hex string.
inline std::string toHex( const std::array< std::byte, 256 / 8 >& data )
{
	std::string str {};
	str.reserve( data.size() );
	for ( auto i : data ) str += format_ns::format( "{:02x}", static_cast< std::uint8_t >( i ) );
	return str;
}

} // namespace idhan::crypto