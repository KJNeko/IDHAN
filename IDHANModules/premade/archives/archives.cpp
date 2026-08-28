#include "archives.hpp"

#include <openssl/evp.h>

#include <archive.h>
#include <archive_entry.h>
#include <array>
#include <chardet.h>
#include <cstring>
#include <cwchar>
#include <expected>
#include <iconv.h>
#include <langinfo.h>
#include <memory>
#include <span>
#include <string>
#include <string_view>

#include "ModuleBase.hpp"
#include "spdlog/spdlog.h"

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

	std::string out_string { out_buffer.data(), out_buffer.size() - out_size };

	return out_string;
}

std::expected< std::vector< std::byte >, idhan::ModuleError > readArchiveEntryData( archive* a )
{
	std::vector< std::byte > data {};

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

std::int64_t ArchiveModuleReader::onRead( archive* const a, void* const client_data, const void** const buffer )
{
	auto* const self { static_cast< ArchiveModuleReader* >( client_data ) };

	*buffer = self->m_chunk.data();

	const auto count { self->m_file->read( std::span< std::byte > { self->m_chunk }, self->m_position ) };

	if ( !count )
	{
		self->m_error = count.error();
		archive_set_error( a, EIO, "%s", self->m_error.c_str() );
		return -1;
	}

	self->m_position += *count;

	return static_cast< std::int64_t >( *count );
}

std::expected< void, idhan::ModuleError > ArchiveModuleReader::open( archive* const a )
{
	if ( archive_read_open( a, this, nullptr, &ArchiveModuleReader::onRead, nullptr ) != ARCHIVE_OK )
	{
		const char* err { archive_error_string( a ) };
		return std::unexpected( idhan::ModuleError { err ? err : "archive_read_open failed" } );
	}

	return {};
}

std::expected< void, idhan::ModuleError > writeArchiveEntryData( archive* const a, idhan::ModuleSink& out )
{
	constexpr std::size_t chunk_size { 64 * 1024 };
	std::array< std::byte, chunk_size > buffer {};

	la_ssize_t read { 0 };
	while ( ( read = archive_read_data( a, buffer.data(), buffer.size() ) ) > 0 )
	{
		if ( const auto written {
				 out.write( std::span< const std::byte > { buffer.data(), static_cast< std::size_t >( read ) } ) };
		     !written )
			return std::unexpected( written.error() );
	}

	if ( read < 0 )
	{
		const char* err { archive_error_string( a ) };
		return std::unexpected( idhan::ModuleError { err ? err : "archive_read_data failed" } );
	}

	return {};
}

std::expected< ArchiveEntryHash, idhan::ModuleError > hashArchiveEntryData( archive* a )
{
	const std::unique_ptr< EVP_MD_CTX, void ( * )( EVP_MD_CTX* ) > ctx {
		EVP_MD_CTX_new(), []( EVP_MD_CTX* ptr ) { EVP_MD_CTX_free( ptr ); }
	};
	if ( !ctx ) return std::unexpected( idhan::ModuleError { "EVP_MD_CTX_new() failed" } );

	if ( EVP_DigestInit_ex( ctx.get(), EVP_sha256(), nullptr ) != 1 )
		return std::unexpected( idhan::ModuleError { "EVP_DigestInit_ex() failed" } );

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
	const auto iconv_cd { iconv_open( "UTF-8", "WCHAR_T" ) };
	if ( iconv_cd == reinterpret_cast< iconv_t >( -1 ) )
		return std::unexpected( idhan::ModuleError { "Failed to open iconv for WCHAR_T" } );

	const std::size_t in_bytes { std::wcslen( str ) * sizeof( wchar_t ) };
	std::vector< char > in_buffer( in_bytes );
	std::memcpy( in_buffer.data(), str, in_bytes );
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

EntryNameResult entryFilename( archive_entry* entry, std::expected< std::string, idhan::ModuleError >& out )
{
	if ( const char* filename_raw = archive_entry_pathname( entry ); filename_raw != nullptr )
	{
		out = sanitizeEncoding( filename_raw );
	}
	else if ( const char* utf8_filename_raw = archive_entry_pathname_utf8( entry ); utf8_filename_raw != nullptr )
	{
		out = sanitizeEncoding( utf8_filename_raw );
	}
	else if ( const wchar_t* w_filename_raw = archive_entry_pathname_w( entry ); w_filename_raw != nullptr )
	{
		// Wide-only name: convert it rather than dropping the entry.
		out = wideToUtf8( w_filename_raw );
	}
	else
	{
		spdlog::warn(
			"No file name for item in archive? Maybe encrypted but not flagged as such? (locale charset: {})",
			nl_langinfo( CODESET ) );
		return EntryNameResult::UNNAMED;
	}

	return out ? EntryNameResult::OK : EntryNameResult::FAILED;
}

static std::size_t digitRunLength( const std::string_view text, const std::size_t start )
{
	std::size_t end { start };
	while ( end < text.size() && text[ end ] >= '0' && text[ end ] <= '9' ) ++end;
	return end - start;
}

//! The digits of the run at [start, start + length), minus leading zeros. Never empty: an all-zero
//! run keeps one digit.
static std::string_view significantDigits(
	const std::string_view text,
	const std::size_t start,
	const std::size_t length )
{
	std::size_t first { start };
	const std::size_t end { start + length };
	while ( first + 1 < end && text[ first ] == '0' ) ++first;

	return text.substr( first, end - first );
}

bool naturalLess( const std::string_view lhs, const std::string_view rhs )
{
	std::size_t i { 0 };
	std::size_t j { 0 };

	while ( i < lhs.size() && j < rhs.size() )
	{
		const auto left_digits { digitRunLength( lhs, i ) };
		const auto right_digits { digitRunLength( rhs, j ) };

		if ( left_digits == 0 || right_digits == 0 )
		{
			if ( lhs[ i ] != rhs[ j ] )
				return static_cast< unsigned char >( lhs[ i ] ) < static_cast< unsigned char >( rhs[ j ] );

			++i;
			++j;
			continue;
		}

		const auto left_value { significantDigits( lhs, i, left_digits ) };
		const auto right_value { significantDigits( rhs, j, right_digits ) };

		if ( left_value.size() != right_value.size() ) return left_value.size() < right_value.size();
		if ( left_value != right_value ) return left_value < right_value;

		i += left_digits;
		j += right_digits;
	}

	return ( lhs.size() - i ) < ( rhs.size() - j );
}
