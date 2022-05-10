//
// Created by kj16609 on 5/6/22.
//

#ifndef IDHAN_MIMEPROCESSING_HPP
#define IDHAN_MIMEPROCESSING_HPP

#include <vector>
#include <tuple>
#include <cstring>

#include <type_traits>

int getMimeType( std::vector<char> data );

template < class... Types >
std::tuple<Types...> getMime( std::vector<char> data, size_t offset )
{
	std::tuple<Types...> tup;

	//populate tup using data
	size_t i = 0;

	auto readVar = [&]( auto& var )
	{
		i += sizeof( var );

		memcpy( &var, data.data(), sizeof( var ));
	};


	std::apply( [&]( Types& ... args )
	{
		(( readVar( args )), ...);
	}, tup );

	return tup;
}

#endif //IDHAN_MIMEPROCESSING_HPP
