#include "idhan/http/request/RequestContext.hpp"

namespace idhan::http
{

RequestContext::RequestContext(
	const RequestID id,
	proxygen::HTTPMessage message,
	RequestParameters parameters,
	RequestBody body,
	runtime::WorkRegistration work ) :
  m_id( id ),
  m_message( std::move( message ) ),
  m_parameters( std::move( parameters ) ),
  m_body( std::move( body ) ),
  m_work( std::move( work ) )
{}

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

void RequestContext::setParameterRules( const ParameterRules* rules ) noexcept
{
	m_parameter_rules = rules;
}

const ParameterRules* RequestContext::parameterRules() const noexcept
{
	return m_parameter_rules;
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

} // namespace idhan::http
