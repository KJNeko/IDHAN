//
// Created by kj16609 on 3/25/24.
//

#include "idhan/client/ClientContext.hpp"

#include <asio/connect.hpp>
#include <asio/ssl/verify_mode.hpp>

#include "spdlog/spdlog.h"

namespace idhan
{
	ClientContext::ClientContext( const std::string& host, const std::uint16_t port ) :
	  secure_socket( asio_context, ssl_context ),
	  resolver( asio_context )
	{
		runner = std::jthread(
			[ this ]( std::stop_token token ) -> void
			{
				spdlog::info( "[IDHANClient] Starting asio IO thread" );
				while ( !token.stop_requested() )
				{
					asio_context.run();
				}
				spdlog::info( "[IDHANClient] Shutting down asio IO thread" );
			} );

		//Resolve host
		auto endpoints { resolver.resolve( host, std::to_string( port ) ) };

		try
		{
			//Begin inital connection
			spdlog::info( "[IDHANClient] Starting connection to {}:{}", host, port );
			asio::connect( secure_socket.lowest_layer(), endpoints );

			spdlog::info( "[IDHANClient] Connection established. Attempting handshake" );

			secure_socket.set_verify_mode( asio::ssl::verify_none );
			secure_socket.async_handshake(
				decltype( secure_socket )::client,
				[ this ]( const std::error_code error_code )
				{
					if ( error_code )
						spdlog::error( "[IDHANClient] SSL Handshake failed: {}", error_code.message() );
					else
						spdlog::info( "[IDHANClient] Handshake complete" );
				} );
		}
		catch ( std::exception& e )
		{
			spdlog::error( "Failed to connect to remote host {}:{} due to errror: {}", host, port, e.what() );
		}
	}

	void ClientContext::shutdown()
	{
		(void)runner.request_stop();
		asio_context.stop();
	}

	void ClientContext::waitForShutdown()
	{
		runner.join();
	}

} // namespace idhan
