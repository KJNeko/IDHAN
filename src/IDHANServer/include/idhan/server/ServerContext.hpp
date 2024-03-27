//
// Created by kj16609 on 3/26/24.
//

#pragma once
#include <asio/ip/tcp.hpp>
#include <asio/ssl/context.hpp>
#include <asio/ssl/stream.hpp>

#include <cstdint>
#include <string>
#include <thread>

namespace idhan
{

	struct ClientConnection : std::enable_shared_from_this< ClientConnection >
	{
		asio::ssl::stream< asio::ip::tcp::socket > secure_socket;
		std::array< std::byte, 1024 > in_data_buffer {};

		ClientConnection( asio::ip::tcp::socket&& socket, asio::ssl::context& ssl_context ) :
		  secure_socket( std::forward< decltype( socket ) >( socket ), ssl_context )
		{}

		void startHandshake();

		void sendData( const std::vector< std::byte >& data );
		std::vector< std::byte > recieveData();
		void prepareRead();
		void handleInputData( std::size_t size );
	};

	class ServerContext
	{
		std::jthread runner;
		asio::io_context io_context {};
		asio::ssl::context ssl_context { asio::ssl::context::sslv23 };
		asio::ip::tcp::endpoint endpoint;
		asio::ip::tcp::acceptor acceptor;

		bool accept_new_connections { true };

		std::vector< std::shared_ptr< ClientConnection > > clients {};

	  public:

		constexpr static std::string DEFAULT_LISTEN_HOST { "127.0.0.1" };
		constexpr static std::uint16_t DEFAULT_LISTEN_PORT { 16609 };

		void handleNewConnection( const std::error_code& ec, asio::ip::tcp::socket socket );
		void prepareIncomingAcceptor();
		ServerContext(
			const std::string& listen_address = DEFAULT_LISTEN_HOST, const std::uint16_t port = DEFAULT_LISTEN_PORT );
		~ServerContext();

		asio::ip::address listenAddress() const { return endpoint.address(); }

		asio::ip::port_type listenPort() const { return endpoint.port(); }
	};

} // namespace idhan
