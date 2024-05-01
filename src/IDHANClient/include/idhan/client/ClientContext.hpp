//
// Created by kj16609 on 3/25/24.
//

#pragma once
#include <asio/ip/tcp.hpp>
#include <asio/ssl/context.hpp>
#include <asio/ssl/stream.hpp>

#include <future>
#include <thread>

#include "idhan/MessageHeader.hpp"
#include "idhan/requests/VersionInfo.hpp"

namespace idhan
{

	struct MessageFutureBase
	{};

	class ClientContext
	{
		asio::io_context asio_context {};
		asio::ip::tcp::resolver resolver;
		asio::ssl::context ssl_context { asio::ssl::context::sslv23 };
		asio::ssl::stream< asio::ip::tcp::socket > secure_socket;
		std::jthread runner;

		//! Futures for messages that are to be fullfilled by the server
		std::vector< std::unique_ptr< MessageFutureBase > > futures {};

	  public:

		constexpr static std::uint16_t DEFAULT_LISTEN_PORT { 16609 };

		ClientContext() = delete;

		ClientContext( const std::string& host, const std::uint16_t port = DEFAULT_LISTEN_PORT );

		ClientContext( const ClientContext& other ) = delete;
		ClientContext& operator=( const ClientContext& other ) = delete;

		//! Sends a message request to the server and creates a future for a response
		template < typename T >
			requires is_respondable< T >
		std::future< typename T::ResponseT > getResponse( const T&& req )
		{
			using ResponseType = typename T::ResponseT;
			std::promise< ResponseType > promise {};

			MessageHeader header {};

			// If the message future return type is void then we should NOT be expecting a response
			if constexpr ( !std::same_as< typename T::ResponseT, void > )
				header.flags |= MessageHeader::EXPECTING_RESPONSE; // Add response expected

			// Set the routing ID for the server to call the right function
			header.routing_id = T::type_id;

			std::future< ResponseType > future { promise.get_future() };

			sendMessage( header, T::serialize( std::forward< const T&& >( req ) ), std::move( promise ) );

			return future;
		}

		std::future< ServerVersionInfoResponse > requestServerVersionInfo();

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