//
// Created by kj16609 on 7/13/25.
//
#pragma once
#include <type_traits>

namespace idhan
{
template < typename T >
concept has_value = requires( T& t ) {
	{ t.value() } -> std::same_as< typename T::value_type& >;
};

template < typename T >
concept has_value_check = requires( T& t ) {
	{ t.has_value() } -> std::same_as< bool >;
};

template < typename T >
concept has_get = requires( T& t ) {
	{ std::get< 0 >( t ) };
};

template < typename T > concept is_tuple = has_get< std::remove_reference_t< T > >;

template < typename T >
concept is_optional = has_value< std::remove_reference_t< T > > && has_value_check< std::remove_reference_t< T > >;

} // namespace idhan