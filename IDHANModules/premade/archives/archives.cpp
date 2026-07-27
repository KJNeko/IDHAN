//
// Created by kj16609 on 11/25/25.
//

#include "archives.hpp"

#include <archive.h>
#include <openssl/evp.h>

#include <array>
#include <chardet.h>
#include <cstring>
#include <cwchar>
#include <expected>
#include <iconv.h>
#include <memory>
#include <string>

#include "ModuleBase.hpp"

std::expected< std::string, idhan::ModuleError > encoding( const char* str )
{
	DetectObj* obj { nullptr };

	if ( ( obj = detect_obj_init() ) == nullptr )
	{
		return std::unexpected( idhan::ModuleError { "Failed to load chardet" } );
	}

	switch ( detect( str, &obj ) )
	{
		case CHARDET_OUT_OF_MEMORY:
			detect_obj_free( &obj );
			return std::unexpected( idhan::ModuleError { "chardet OOM error" } );
		case CHARDET_NULL_OBJECT:
			detect_obj_free( &obj );
			return std::unexpected( idhan::ModuleError { "Null chardet object" } );
		case CHARDET_NO_RESULT:
			detect_obj_free( &obj );
			return std::unexpected( idhan::ModuleError { "chardet no result" } );
		default:
			break;
	}

	const char* encoding_str { obj->encoding };
	if ( !encoding_str )
	{
		detect_obj_free( &obj );
		return std::unexpected( idhan::ModuleError { "chardet returned null encoding" } );
	}
	std::string encoding { encoding_str };

	detect_obj_free( &obj );

	return encoding;
}

std::string convertToICONVEncodingName( const std::string str )
{
	std::unordered_map< std::string, std::string > map { { "Shift_JIS", "SHIFT_JIS" } };

	if ( auto itter = map.find( str ); itter != map.end() ) return itter->second;

	return str;
}

std::expected< std::string, idhan::ModuleError > sanitizeEncoding( const char* str )
{
	const auto str_encoding { encoding( str ) };
	if ( !str_encoding ) return std::unexpected( str_encoding.error() );

	const auto fixed_str_encoding { convertToICONVEncodingName( *str_encoding ) };

	const auto iconv_cd { iconv_open( "UTF-8", fixed_str_encoding.c_str() ) };
	if ( iconv_cd == reinterpret_cast< iconv_t >( -1 ) )
	{
		perror( "iconv_open" );
		return std::unexpected( idhan::ModuleError { "Failed to open iconv" } );
	}

	std::vector< char > in_buffer {};
	std::vector< char > out_buffer {};
	in_buffer.resize( strlen( str ) );
	std::memcpy( in_buffer.data(), str, in_buffer.size() );
	out_buffer.resize( strlen( str ) * 4 );

	auto* in_buffer_ptr { in_buffer.data() };
	auto* out_buffer_ptr { out_buffer.data() };
	std::size_t in_size { in_buffer.size() };
	std::size_t out_size { out_buffer.size() };

	const auto result { iconv( iconv_cd, &in_buffer_ptr, &in_size, &out_buffer_ptr, &out_size ) };

	iconv_close( iconv_cd );

	if ( result == static_cast< std::size_t >( -1 ) )
		return std::unexpected( idhan::ModuleError { "iconv conversion failed" } );

	// use the byte count iconv actually wrote rather than strlen(): out_buffer isn't guaranteed
	// to contain a null terminator if the conversion filled it exactly
	std::string out_string { out_buffer.data(), out_buffer.size() - out_size };

	return out_string;
}

std::expected< std::vector< std::byte >, idhan::ModuleError > readArchiveEntryData( archive* a )
{
	std::vector< std::byte > data {};

	// archive_read_data may return fewer bytes than requested (short read) and does not need the
	// entry size to be known in advance, so grow the buffer as we go rather than trusting
	// archive_entry_size() for a single allocate-and-read.
	constexpr std::size_t chunk_size { 64 * 1024 };
	std::array< std::byte, chunk_size > buffer {};

	la_ssize_t read { 0 };
	while ( ( read = archive_read_data( a, buffer.data(), buffer.size() ) ) > 0 )
	{
		data.insert( data.end(), buffer.begin(), buffer.begin() + read );
	}

	if ( read < 0 )
	{
		const char* err { archive_error_string( a ) };
		return std::unexpected( idhan::ModuleError { err ? err : "archive_read_data failed" } );
	}

	return data;
}

std::expected< ArchiveEntryHash, idhan::ModuleError > hashArchiveEntryData( archive* a )
{
	const std::unique_ptr< EVP_MD_CTX, void ( * )( EVP_MD_CTX* ) > ctx {
		EVP_MD_CTX_new(), []( EVP_MD_CTX* ptr ) { EVP_MD_CTX_free( ptr ); }
	};
	if ( !ctx ) return std::unexpected( idhan::ModuleError { "EVP_MD_CTX_new() failed" } );

	if ( EVP_DigestInit_ex( ctx.get(), EVP_sha256(), nullptr ) != 1 )
		return std::unexpected( idhan::ModuleError { "EVP_DigestInit_ex() failed" } );

	// Fold each chunk into the digest as it is read; never hold more than one chunk, so peak memory
	// stays O(chunk_size) no matter how large the entry decompresses to. archive_read_data may return
	// short reads and does not need the entry size known in advance.
	constexpr std::size_t chunk_size { 64 * 1024 };
	std::array< std::byte, chunk_size > buffer {};

	ArchiveEntryHash result {};

	la_ssize_t read { 0 };
	while ( ( read = archive_read_data( a, buffer.data(), buffer.size() ) ) > 0 )
	{
		if ( EVP_DigestUpdate( ctx.get(), buffer.data(), static_cast< std::size_t >( read ) ) != 1 )
			return std::unexpected( idhan::ModuleError { "EVP_DigestUpdate() failed" } );

		result.m_size += static_cast< std::size_t >( read );
	}

	if ( read < 0 )
	{
		const char* err { archive_error_string( a ) };
		return std::unexpected( idhan::ModuleError { err ? err : "archive_read_data failed" } );
	}

	unsigned char hash[ EVP_MAX_MD_SIZE ];
	unsigned int hash_length { 0 };

	if ( EVP_DigestFinal_ex( ctx.get(), hash, &hash_length ) != 1 )
		return std::unexpected( idhan::ModuleError { "EVP_DigestFinal_ex() failed" } );

	if ( hash_length != result.m_hash.size() )
		return std::unexpected( idhan::ModuleError { "SHA-256 produced an unexpected digest length" } );

	std::memcpy( result.m_hash.data(), hash, hash_length );

	return result;
}

std::expected< std::string, idhan::ModuleError > wideToUtf8( const wchar_t* str )
{
	// "WCHAR_T" is iconv's name for the platform's wchar_t encoding, so this is portable across
	// Linux (UTF-32) and Windows (UTF-16) without hand-rolling the codec.
	const auto iconv_cd { iconv_open( "UTF-8", "WCHAR_T" ) };
	if ( iconv_cd == reinterpret_cast< iconv_t >( -1 ) )
		return std::unexpected( idhan::ModuleError { "Failed to open iconv for WCHAR_T" } );

	const std::size_t in_bytes { std::wcslen( str ) * sizeof( wchar_t ) };
	std::vector< char > in_buffer( in_bytes );
	std::memcpy( in_buffer.data(), str, in_bytes );
	// UTF-8 needs at most 4 bytes per code point; sizeof(wchar_t) >= 2 already covers that per
	// input unit, so 2x plus slack is comfortably enough.
	std::vector< char > out_buffer( in_bytes * 2 + 4 );

	auto* in_buffer_ptr { in_buffer.data() };
	auto* out_buffer_ptr { out_buffer.data() };
	std::size_t in_size { in_buffer.size() };
	std::size_t out_size { out_buffer.size() };

	const auto result { iconv( iconv_cd, &in_buffer_ptr, &in_size, &out_buffer_ptr, &out_size ) };

	iconv_close( iconv_cd );

	if ( result == static_cast< std::size_t >( -1 ) )
		return std::unexpected( idhan::ModuleError { "iconv WCHAR_T conversion failed" } );

	return std::string { out_buffer.data(), out_buffer.size() - out_size };
}