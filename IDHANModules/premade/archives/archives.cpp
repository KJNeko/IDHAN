//
// Created by kj16609 on 11/25/25.
//

#include "archives.hpp"

#include <chardet.h>
#include <expected>
#include <iconv.h>
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

	std::string out_string { out_buffer.data(), strlen( out_buffer.data() ) };

	return out_string;
}