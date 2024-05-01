//
// Created by kj16609 on 3/27/24.
//

#pragma once
#include <cstdint>

#include "NetRequestInfo.hpp"
#include "NetResponseInfo.hpp"
#include "concepts.hpp"

namespace idhan
{
	constexpr std::size_t VersionInfoNetID { 1 };

	struct ServerVersionInfoResponse : public NetResponseInfo< ServerVersionInfoResponse, VersionInfoNetID >
	{
		static std::vector< std::byte > serialize( const ServerVersionInfoResponse& );
		static ServerVersionInfoResponse deserialize( const std::vector< std::byte >& );
	};

	struct ServerVersionInfoRequest : public NetRequestInfo< ServerVersionInfoRequest, VersionInfoNetID >
	{
		using ResponseT = ServerVersionInfoResponse;

		//! Executed on the server
		static ResponseT response( ServerVersionInfoRequest& req );

		static std::vector< std::byte > serialize( const ServerVersionInfoRequest& );
		static ServerVersionInfoRequest deserialize( const std::vector< std::byte >& );
	};

} // namespace idhan
