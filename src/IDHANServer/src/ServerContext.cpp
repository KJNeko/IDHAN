//
// Created by kj16609 on 3/26/24.
//

#include "idhan/server/ServerContext.hpp"

#include <asio/ssl/stream.hpp>

#include <filesystem>

#include "spdlog/spdlog.h"

namespace idhan
{

	void ServerContext::handleNewConnection( const std::error_code& ec, asio::ip::tcp::socket socket )
	{
		if ( !accept_new_connections ) return socket.close();

		prepareIncomingAcceptor();
		const auto address { socket.remote_endpoint().address() };
		spdlog::info( "[IDHANServer]: Handling new connection from host: {}", address.to_string() );
		if ( ec )
		{
			spdlog::
				error( "[IDHANServer] Aborted connection from {} due to error: {}", address.to_string(), ec.message() );
			return;
		}

		this->clients.emplace_back( std::make_shared< ClientConnection >( std::move( socket ), ssl_context ) )
			->startHandshake();
	}

	void ClientConnection::startHandshake()
	{
		auto self { shared_from_this() };
		secure_socket.async_handshake(
			asio::ssl::stream_base::server,
			[ this, self ]( const std::error_code& error_code )
			{
				if ( error_code )
					spdlog::error( "[IDHANServer] Failed to do handshake: {}", error_code.message() );
				else
				{
					spdlog::info( "[IDHANServer] Handshake completed." );

					prepareRead();
				}
			} );
		spdlog::info( "[IDHANServer] Handshake for new connection queued" );
	}

	void ClientConnection::prepareRead()
	{
		auto self { shared_from_this() };
		secure_socket.async_read_some(
			asio::buffer( in_data_buffer ),
			[ this, self ]( const std::error_code& ec, std::size_t length ) -> void
			{
				spdlog::info( "Received message with length: {}", length );
				if ( ec )
				{
					spdlog::error( "Error while reading data from remote: {}", ec.message() );
				}
				else
				{
					handleInputData( length );
					prepareRead();
				}
			} );
	}

	void ClientConnection::handleInputData( std::size_t size )
	{}

	void ServerContext::prepareIncomingAcceptor()
	{
		spdlog::info( "[IDHANServer] Prepared acceptor for next incoming connection" );
		acceptor.async_accept(
			std::bind( &ServerContext::handleNewConnection, this, std::placeholders::_1, std::placeholders::_2 ) );
	}

	ServerContext::ServerContext( const std::string& listen_address, const std::uint16_t port ) :
	  io_context( 2 ),
	  endpoint( asio::ip::address::from_string( listen_address ), port ),
	  acceptor( io_context, endpoint )
	{
		if ( !std::filesystem::exists( "server.pem" ) )
			throw std::runtime_error(
				std::string( "No server.pem file exists at " ) + std::filesystem::current_path().string() );
		ssl_context.use_certificate_chain_file( "server.pem" );
		ssl_context.use_private_key_file( "server.pem", asio::ssl::context::pem );

		prepareIncomingAcceptor();

		spdlog::info( "[IDHANServer] Listening on port {}:{}", listen_address, port );

		runner = std::jthread(
			[ this ]( std::stop_token token ) -> void
			{
				spdlog::info( "[IDHANServer] Starting asio IO thread" );
				while ( !token.stop_requested() )
				{
					io_context.run();
				}
				spdlog::info( "[IDHANServer] Shutting down asio IO thread" );
			} );
	}

	ServerContext::~ServerContext()
	{
		runner.request_stop();
		io_context.stop();
	}

} // namespace idhan
