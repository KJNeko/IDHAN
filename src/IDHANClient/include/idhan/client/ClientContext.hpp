//
// Created by kj16609 on 3/25/24.
//

#pragma once
#include <asio/ip/tcp.hpp>
#include <asio/ssl/context.hpp>
#include <asio/ssl/stream.hpp>

#include <thread>

namespace idhan
{
	class ClientContext
	{
		asio::io_context asio_context {};
		asio::ip::tcp::resolver resolver;
		asio::ssl::context ssl_context { asio::ssl::context::sslv23 };
		asio::ssl::stream< asio::ip::tcp::socket > secure_socket;
		std::jthread runner;

	  public:

		constexpr static std::uint16_t DEFAULT_LISTEN_PORT { 16609 };

		ClientContext() = delete;

		ClientContext( const std::string& host, const std::uint16_t port = DEFAULT_LISTEN_PORT );

		ClientContext( const ClientContext& other ) = delete;
		ClientContext& operator=( const ClientContext& other ) = delete;

		ClientContext( ClientContext&& other ) noexcept = delete;
		ClientContext& operator=( ClientContext&& other ) = delete;

		void shutdown();
		void waitForShutdown();

		~ClientContext()
		{
			shutdown();
			waitForShutdown();
		}
	};
} // namespace idhan