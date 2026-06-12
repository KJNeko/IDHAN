//
// Created by kj16609 on 9/8/23.
//

#pragma once

#include <concepts>
#include <sqlite3.h>

#include "concepts.hpp"
#include "idhan/logging/logger.hpp"

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