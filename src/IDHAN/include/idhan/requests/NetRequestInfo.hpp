//
// Created by kj16609 on 3/28/24.
//

#pragma once
#include "concepts.hpp"

namespace idhan
{

	template < typename T, std::size_t TypeID >
	struct NetRequestInfo
	{
		static constexpr std::size_t type_id { TypeID };

		static std::vector< std::byte > response( const std::vector< std::byte >& data )
		{
			const T t { T::deserialize( data ) };
			const typename T::ResponseT response { T::response( t ) };
			return T::ResponseT::serialize( response );
		}
	};

} // namespace idhan
