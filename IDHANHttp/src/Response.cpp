#include "idhan/http/Response.hpp"

#include <string>

namespace idhan::http
{

Response::Response()
{
	message.setStatusCode( 200 );
	message.setStatusMessage( proxygen::HTTPMessage::getDefaultReason( 200 ) );
	message.getHeaders().set( "Server", "IDHAN" );
}

Response::~Response() = default;
Response::Response( Response&& ) noexcept = default;
Response& Response::operator=( Response&& ) noexcept = default;

Response Response::error( const std::uint16_t status, std::string text )
{
	Response response;
	response.message.setStatusCode( status );
	response.message.setStatusMessage( proxygen::HTTPMessage::getDefaultReason( status ) );
	response.message.getHeaders().set( "Content-Type", "text/plain; charset=utf-8" );
	response.body = folly::IOBuf::copyBuffer( text );
	return response;
}

Response Response::json( const std::uint16_t status, std::unique_ptr< folly::IOBuf > body )
{
	return bytes( status, std::move( body ), "application/json" );
}

Response Response::bytes( const std::uint16_t status, std::unique_ptr< folly::IOBuf > body, std::string content_type )
{
	Response response;
	response.message.setStatusCode( status );
	response.message.setStatusMessage( proxygen::HTTPMessage::getDefaultReason( status ) );
	response.message.getHeaders().set( "Content-Type", std::move( content_type ) );
	response.body = std::move( body );
	return response;
}

} // namespace idhan::http
