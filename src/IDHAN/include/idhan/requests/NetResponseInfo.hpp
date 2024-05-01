//
// Created by kj16609 on 3/28/24.
//

#pragma once

namespace idhan
{
	template < typename T, std::size_t TypeID >
	struct NetResponseInfo
	{
		static constexpr std::size_t type_id { TypeID };
	};
} // namespace idhan