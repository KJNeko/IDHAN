#include "idhan/http/response/Response.hpp"

#include <string>

#include "idhan/http/response/ReflectedResponses.hpp"

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
	auto json_e { serializeJson( ErrorResponse { std::move( text ) } ) };
	if ( !json_e.has_value() )
		return json( status, folly::IOBuf::copyBuffer( R"({"error":"Unable to serialize error message"})" ) );

	return json( status, folly::IOBuf::copyBuffer( json_e.value() ) );
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
