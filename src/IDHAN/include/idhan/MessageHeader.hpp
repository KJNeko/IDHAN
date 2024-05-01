//
// Created by kj16609 on 3/27/24.
//

#pragma once
#include <cstdint>
#include <vector>

#include "requests/concepts.hpp"

namespace idhan
{

	constexpr std::uint64_t IDHAN_SIG_FULL { 0x49'44'48'41'4e'00'00'00 };

	struct MessageHeader
	{
		std::size_t routing_id;
		std::uint64_t flags { 0 };

		enum Flags
		{
			EXPECTING_RESPONSE
		};

		static std::vector< std::byte > serialize( const MessageHeader& msg );
		static MessageHeader desieralize( const std::vector< std::byte >& data );
	};

} // namespace idhan
