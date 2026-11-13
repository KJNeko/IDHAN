#include "js/QuickJSRegexProvider.hpp"

#include <array>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

extern "C" {
#include <cutils.h>
#include <libregexp.h>
}

namespace idhan::downloader::quickjs
{
thread_local JSContext* QuickJSRegexProvider::s_context {};

QuickJSRegexProvider::Bytecode::~Bytecode()
{
	js_free_rt( runtime, data );
}

QuickJSRegexProvider::Scope::Scope( JSContext* context ) noexcept : m_previous( std::exchange( s_context, context ) )
{}

QuickJSRegexProvider::Scope::~Scope()
{
	s_context = m_previous;
}

std::string escapeURLPatternHyphens( const std::string_view pattern )
{
	std::string output;
	std::size_t class_depth {};

	for ( std::size_t index {}; index < pattern.size(); ++index )
	{
		const char character { pattern[ index ] };

		if ( character == '\\' && index + 1 < pattern.size() )
		{
			output.push_back( character );
			output.push_back( pattern[ ++index ] );
			continue;
		}

		if ( character == '[' ) ++class_depth;

		if ( character == ']' && class_depth > 0 ) --class_depth;

		// This QuickJS version applies v-mode class restrictions to literal hyphens outside
		// classes too. Hex escaping preserves e.g. https://i.pximg.net/img-original/img/x.png.
		if ( character == '-' && class_depth == 0 )
		{
			output.append( "\\x2d" );
			continue;
		}

		output.push_back( character );
	}

	return output;
}

std::optional< QuickJSRegexProvider::regex_type > QuickJSRegexProvider::create_instance(
	const std::string_view pattern,
	const bool ignore_case )
{
	auto bytecode { std::make_shared< Bytecode >() };
	bytecode->runtime = JS_GetRuntime( s_context );
	std::array< char, 256 > error {};
	int length {};
	const int flags { LRE_FLAG_UNICODE_SETS | ( ignore_case ? LRE_FLAG_IGNORECASE : 0 ) };
	const std::string source { escapeURLPatternHyphens( pattern ) };

	bytecode->data = lre_compile(
		&length, error.data(), static_cast< int >( error.size() ), source.c_str(), source.size(), flags, s_context );

	if ( bytecode->data == nullptr )
	{
		if ( std::string_view { error.data() }.find( "memory" ) != std::string_view::npos )
		{
			JS_ThrowOutOfMemory( s_context );
			return std::nullopt;
		}

		JS_ThrowTypeError( s_context, "Invalid URLPattern regular expression '%s': %s", source.c_str(), error.data() );
		return std::nullopt;
	}

	return bytecode;
}

std::u16string urlPatternUTF16( const std::string_view input )
{
	// libregexp computes its UTF-16 byte length in a signed int.
	if ( input.size() > static_cast< std::size_t >( std::numeric_limits< int >::max() ) / sizeof( char16_t ) )
	{
		throw std::length_error( "URLPattern input is too long" );
	}

	std::u16string output;
	output.reserve( input.size() );

	if ( input.empty() )
	{
		return output;
	}

	const auto* cursor { reinterpret_cast< const std::uint8_t* >( input.data() ) };
	const auto* end { cursor + input.size() };

	while ( cursor < end )
	{
		const int code_point { unicode_from_utf8( cursor, static_cast< int >( end - cursor ), &cursor ) };

		if ( code_point < 0 || code_point > 0x10ffff )
		{
			throw std::invalid_argument( "Invalid UTF-8 in URLPattern input" );
		}

		if ( code_point >= 0xd800 && code_point <= 0xdfff )
		{
			throw std::invalid_argument( "Unpaired surrogate in URLPattern input" );
		}

		if ( code_point <= 0xffff )
		{
			output.push_back( static_cast< char16_t >( code_point ) );
			continue;
		}

		const auto value { static_cast< unsigned int >( code_point - 0x10000 ) };
		output.push_back( static_cast< char16_t >( 0xd800 + ( value >> 10 ) ) );
		output.push_back( static_cast< char16_t >( 0xdc00 + ( value & 0x3ff ) ) );
	}

	return output;
}

std::string urlPatternUTF8( const char16_t* begin, const char16_t* end )
{
	std::string output;

	while ( begin < end )
	{
		unsigned int code_point { *begin++ };

		if ( code_point >= 0xd800 && code_point <= 0xdbff && begin < end && *begin >= 0xdc00 && *begin <= 0xdfff )
		{
			code_point = 0x10000 + ( ( code_point - 0xd800 ) << 10 ) + ( *begin++ - 0xdc00 );
		}

		if ( code_point >= 0xd800 && code_point <= 0xdfff )
		{
			code_point = 0xfffd;
		}

		std::array< std::uint8_t, 4 > encoded {};
		const int length { unicode_to_utf8( encoded.data(), code_point ) };
		output.append( reinterpret_cast< const char* >( encoded.data() ), static_cast< std::size_t >( length ) );
	}

	return output;
}

std::optional< std::vector< std::optional< std::string > > > QuickJSRegexProvider::regex_search(
	const std::string_view input,
	const regex_type& pattern ) noexcept
{
	try
	{
		if ( JS_HasException( s_context ) )
		{
			return std::nullopt;
		}

		const auto text { urlPatternUTF16( input ) };
		std::vector< std::uint8_t* > captures( static_cast< std::size_t >( lre_get_alloc_count( pattern->data ) ) );
		const auto* buffer { reinterpret_cast< const std::uint8_t* >( text.data() ) };
		const int result {
			lre_exec( captures.data(), pattern->data, buffer, 0, static_cast< int >( text.size() ), 1, s_context )
		};

		if ( result == LRE_RET_TIMEOUT )
		{
			JS_ThrowInternalError( s_context, "URLPattern regular expression execution interrupted" );
			return std::nullopt;
		}

		if ( result < 0 )
		{
			JS_ThrowOutOfMemory( s_context );
			return std::nullopt;
		}

		if ( result != 1 )
		{
			return std::nullopt;
		}

		const int count { lre_get_capture_count( pattern->data ) };
		std::vector< std::optional< std::string > > groups;
		groups.reserve( static_cast< std::size_t >( count - 1 ) );

		// Ada expects captures without the full match at index zero. Keep absent
		// optional groups in place so later captures retain their names.
		for ( int index { 1 }; index < count; ++index )
		{
			const auto* begin { captures[ 2 * index ] };
			const auto* end { captures[ 2 * index + 1 ] };

			if ( begin == nullptr )
			{
				groups.emplace_back( std::nullopt );
				continue;
			}

			groups.emplace_back( urlPatternUTF8(
				reinterpret_cast< const char16_t* >( begin ), reinterpret_cast< const char16_t* >( end ) ) );
		}

		return groups;
	}
	catch ( const std::bad_alloc& )
	{
		JS_ThrowOutOfMemory( s_context );
	}
	catch ( const std::exception& error )
	{
		JS_ThrowInternalError( s_context, "%s", error.what() );
	}
	catch ( ... )
	{
		JS_ThrowInternalError( s_context, "URLPattern regular expression execution failed" );
	}

	return std::nullopt;
}

bool QuickJSRegexProvider::regex_match( const std::string_view input, const regex_type& pattern ) noexcept
{
	return regex_search( input, pattern ).has_value();
}
} // namespace idhan::downloader::quickjs
