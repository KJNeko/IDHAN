//
// Created by kj16609 on 2/21/24.
//

#include <catch2/catch_all.hpp>

#include "idhan/client/ClientContext.hpp"
#include "idhan/server/ServerContext.hpp"

using namespace idhan;

TEST_CASE( "Server setup", "[server][network]" )
{
	WHEN( "Default constructed" )
	{
		ServerContext server_ctx {};

		THEN( "The host should be \"localhost\"" )
		{
			REQUIRE(
				server_ctx.listenAddress() == asio::ip::address::from_string( ServerContext::DEFAULT_LISTEN_HOST ) );
		}

		THEN( "The port should be 16609" )
		{
			REQUIRE( server_ctx.listenPort() == ServerContext::DEFAULT_LISTEN_PORT );
		}

		THEN( "A client should be able to connect" )
		{
			ClientContext client { ServerContext::DEFAULT_LISTEN_HOST, ClientContext::DEFAULT_LISTEN_PORT };
			using namespace std::chrono_literals;
			std::this_thread::sleep_for( 100ms );
			AND_THEN( "A second client should be able to connect" )
			{
				ClientContext second_client { ServerContext::DEFAULT_LISTEN_HOST, ClientContext::DEFAULT_LISTEN_PORT };
			}
		}
	}
}
