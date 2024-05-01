//
// Created by kj16609 on 3/27/24.
//

#pragma once
#include <cstddef>
#include <vector>

namespace idhan
{
	template < typename T >
	concept is_serializable = requires( T t ) {
		{
			T::serialize( t )
		} -> std::same_as< std::vector< std::byte > >;
	};

	template < typename T >
	concept is_deserializable = requires( T t ) {
		{
			T::deserialize( std::declval< std::vector< std::byte > >() )
		} -> std::same_as< T >;
	};

	template < typename T >
	concept is_respondable = requires( T t ) {
		typename T::ResponseT;
		{
			T::response( t )
		} -> std::same_as< typename T::ResponseT >;
	};

} // namespace idhan
