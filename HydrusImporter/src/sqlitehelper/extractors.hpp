//
// Created by kj16609 on 9/8/23.
//

#pragma once
#ifndef ATLASGAMEMANAGER_EXTRACTORS_HPP
#define ATLASGAMEMANAGER_EXTRACTORS_HPP

#include <concepts>
#include <sqlite3.h>

#include "concepts.hpp"
#include "idhan/logging/logger.hpp"

// template < std::uint64_t, typename T >
// void extract( sqlite3_stmt*, T& ) = delete;

/*
template < std::uint64_t index, typename T >
	requires std::same_as< std::string, T >
void extract( [[maybe_unused]] sqlite3_stmt* stmt, [[maybe_unused]] std::string& t ) noexcept
{
	static_assert( false, "You should use std::string_view instead of std::string to reduce the need to memove/copy" );
}

template < std::uint64_t index, typename T >
	requires( !std::move_constructible< T > )
void extract( [[maybe_unused]] sqlite3_stmt* stmt, [[maybe_unused]] std::string& t ) noexcept
{
	static_assert( false, "T is not move constructable" );
}
*/

/*
template < std::uint64_t index, typename T >
	requires std::is_integral_v< T >
void extract( sqlite3_stmt* stmt, T& t ) noexcept
{
	//If the size of type T is bigger then 32 bits. Use int64
	if constexpr ( sizeof( T ) > ( 32 / 8 ) )
	{
		if constexpr ( std::same_as< T, sqlite3_int64 > )
			t = sqlite3_column_int64( stmt, index );
		else
			t = static_cast< T >( sqlite3_column_int64( stmt, index ) );
	}
	else
	{
		if constexpr ( std::same_as< T, int > )
			t = sqlite3_column_int( stmt, index );
		else
			t = static_cast< T >( sqlite3_column_int( stmt, index ) );
	}
}

template < std::uint64_t index, typename T >
	requires std::is_same_v< T, std::string >
void extract( sqlite3_stmt* stmt, std::string& str ) noexcept
{
	const unsigned char* const data { sqlite3_column_text( stmt, index ) };

	str = {};
	if ( data ) str = { reinterpret_cast< const char* const >( data ) };
}

template < std::uint64_t index >
void extract( sqlite3_stmt* stmt, std::vector< std::byte >& out ) noexcept
{
	const void* const data { sqlite3_column_blob( stmt, index ) };
	out.clear();
	if ( data ) std::memcpy( out.data(), data, out.size() );
}

template < std::uint64_t index, typename T >
	requires std::is_same_v< T, std::u8string_view >
void extract( sqlite3_stmt* stmt, std::u8string_view& t ) noexcept
{
	const unsigned char* const txt { sqlite3_column_text( stmt, index ) };

	if ( txt == nullptr )
	{
		t = {};
		return;
	}

	const auto len { strlen( reinterpret_cast< const char* const >( txt ) ) };
	t = std::u8string_view( reinterpret_cast< const char8_t* const >( txt ), len );
}

template < std::uint64_t index, typename T >
	requires std::is_same_v< T, std::string_view >
void extract( sqlite3_stmt* stmt, std::string_view& t ) noexcept
{
#ifdef __linux__
	const unsigned char* const txt { sqlite3_column_text( stmt, index ) };
#else
	const unsigned char* const txt { reinterpret_cast< const unsigned char* >( sqlite3_column_text16( stmt, index ) ) };
#endif

	if ( txt == nullptr )
	{
		t = {};
		return;
	}

	const auto len { strlen( reinterpret_cast< const char* const >( txt ) ) };
	t = std::string_view( reinterpret_cast< const char* const >( txt ), len );
}

template < std::uint64_t index, typename T >
	requires std::is_same_v< T, std::filesystem::path >
void extract( sqlite3_stmt* stmt, std::filesystem::path& t ) noexcept
{
	std::string_view path_str;
	extract< index, std::string_view >( stmt, path_str );
	t = path_str;
}

template < std::uint64_t index, typename T >
	requires std::is_same_v< T, QString >
void extract( sqlite3_stmt* stmt, QString& t ) noexcept
{
	const unsigned char* const txt { sqlite3_column_text( stmt, index ) };

	if ( txt == nullptr )
	{
		t = {};
		return;
	}

	t = QString::fromUtf8( txt );
}
template < std::uint64_t index, typename T >
	requires std::is_same_v< T, std::vector< std::byte > >
void extract( sqlite3_stmt* stmt, std::vector< std::byte >& t ) noexcept
{
	const void* data { sqlite3_column_blob( stmt, index ) };
	const std::size_t size { static_cast< std::size_t >( sqlite3_column_bytes( stmt, index ) ) };

	t.resize( size );
	std::memcpy( t.data(), data, size );
}

*/

template < typename T, std::size_t index >
T extractInt32( sqlite3_stmt* stmt ) noexcept
{
	return static_cast< T >( sqlite3_column_int( stmt, index ) );
}

template < typename T, std::size_t index >
T extractInt64( sqlite3_stmt* stmt ) noexcept
{
	return static_cast< T >( sqlite3_column_int64( stmt, index ) );
}

template < std::size_t index >
std::string_view extractTextView( sqlite3_stmt* stmt ) noexcept
{
	// In order for this to work, Sqlite3 must still own the text memory, There is a flag for this in the setup
	const auto txt { reinterpret_cast< const char* const >( sqlite3_column_text( stmt, index ) ) };
	if ( txt == nullptr )
	{
		return "";
	}

	const auto len { sqlite3_column_bytes( stmt, index ) };
	return std::string_view( txt, static_cast< std::size_t >( len ) );
}

template < std::size_t index >
std::string extractText( sqlite3_stmt* stmt ) noexcept
{
	const auto text { extractTextView< index >( stmt ) };
	return std::string( text.data(), text.size() );
}

template < typename T >
constexpr auto decomposeOptionalType()
{
	if constexpr ( idhan::is_optional< T > )
	{
		return typename T::value_type {};
	}
	else
	{
		return T {};
	}
}

template < std::size_t index, typename T >
T extract( sqlite3_stmt* stmt ) noexcept
{
	constexpr bool is_optional { std::__is_optional_v< T > };
	const auto column_type { sqlite3_column_type( stmt, index ) };
	if constexpr ( is_optional )
	{
		if ( column_type == SQLITE_NULL ) return std::nullopt;
	}
	else
	{
		if ( column_type == SQLITE_NULL ) throw std::runtime_error( "Column is null without an optional" );
	}

	// If it's an optional we want to extract the T type, Not the optional
	using ExtractT = decltype( decomposeOptionalType< T >() );

	if constexpr ( std::same_as< ExtractT, std::string_view > )
	{
		return extractTextView< index >( stmt );
	}
	else if constexpr ( std::same_as< ExtractT, std::string > )
	{
		static_assert( false, "Use std::string_view instead" );
		return extractText< index >( stmt );
	}
	else if constexpr ( std::is_integral_v< ExtractT > )
	{
		if constexpr ( sizeof( ExtractT ) < ( 32 / 8 ) ) // if 32bit then use the 32bit extract
			return extractInt32< ExtractT, index >( stmt );
		else // otherwise use the 64bit extract
			return extractInt64< ExtractT, index >( stmt );
	}
	else
	{
#if __cpp_static_assert >= 202306L
		static_assert( false, std::format( "Unsupported extract type: {}", typeid( ExtractT ).name() ) );
#else
		static_assert( false, "Unsupported extract type" );
#endif
	}
}

template < typename... Args, std::size_t... Indicies >
std::tuple< Args... > extractRow( std::index_sequence< Indicies... >, sqlite3_stmt* stmt ) noexcept
{
	static_assert( sizeof...( Args ) == sizeof...( Indicies ), "Args vs Indicies mismatch" );
	std::tuple< Args... > tpl { extract< Indicies, Args >( stmt )... };

	return tpl;
}

template < typename... Args >
std::tuple< Args... > extractRow( sqlite3_stmt* stmt ) noexcept
{
	if constexpr ( sizeof...( Args ) == 0 )
		return {};
	else
		return extractRow< Args... >( std::index_sequence_for< Args... > {}, stmt );
}

#endif //ATLASGAMEMANAGER_EXTRACTORS_HPP
