#include "idhan/http/RequestContext.hpp"

#include <stdexcept>

namespace idhan::http
{

RequestContext::RequestContext(
	IDHANServerContext& server,
	const RequestID id,
	proxygen::HTTPMessage message,
	RequestParameters parameters,
	RequestBody body,
	runtime::WorkRegistration work ) :
  RequestContext( &server, id, std::move( message ), std::move( parameters ), std::move( body ), std::move( work ) )
{}

RequestContext::RequestContext(
	IDHANServerContext* server,
	const RequestID id,
	proxygen::HTTPMessage message,
	RequestParameters parameters,
	RequestBody body,
	runtime::WorkRegistration work ) :
  m_server( server ),
  m_id( id ),
  m_message( std::move( message ) ),
  m_parameters( std::move( parameters ) ),
  m_body( std::move( body ) ),
  m_work( std::move( work ) )
{}

RequestContext::RequestContext(
	const RequestID id,
	proxygen::HTTPMessage message,
	RequestParameters parameters,
	RequestBody body,
	runtime::WorkRegistration work ) :
  m_server {},
  m_id( id ),
  m_message( std::move( message ) ),
  m_parameters( std::move( parameters ) ),
  m_body( std::move( body ) ),
  m_work( std::move( work ) )
{}

bool RequestContext::hasServerContext() const noexcept
{
	return m_server != nullptr;
}

IDHANServerContext& RequestContext::server() const
{
	if ( !m_server ) throw std::logic_error( "This request has no IDHAN server context" );

	return *m_server;
}

RequestID RequestContext::id() const noexcept
{
	return m_id;
}

const proxygen::HTTPMessage& RequestContext::message() const noexcept
{
	return m_message;
}

const RequestParameters& RequestContext::parameters() const noexcept
{
	return m_parameters;
}

const RequestBody& RequestContext::body() const noexcept
{
	return m_body;
}

void RequestContext::setBody( RequestBody body )
{
	m_body = std::move( body );
}

RequestBody RequestContext::takeBody() &&
{
	return std::move( m_body );
}

runtime::WorkRegistration RequestContext::childWork() const
{
	return m_work.child();
}

bool RequestContext::transportOpen() const noexcept
{
	return m_transport_open;
}

} // namespace idhan::http
