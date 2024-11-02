//
// Created by kj16609 on 7/23/24.
//

#pragma once

#include <cstdint>
#include <string>

namespace idhan
{

	struct IDHANClientConfig
	{
		std::string hostname;
		std::uint16_t port;
	};

	class IDHANClient
	{
	  public:

		IDHANClient( IDHANClientConfig& config );
	};

} // namespace idhan
