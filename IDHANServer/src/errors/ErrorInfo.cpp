//
// Created by kj16609 on 10/27/25.
//
#include "ErrorInfo.hpp"

namespace idhan
{

std::shared_ptr< ErrorInfo > ErrorInfo::setCode( const drogon::HttpStatusCode code )
{
	this->code = code;
	return shared_from_this();
}

drogon::HttpResponsePtr ErrorInfo::genResponse() const
{
	return internal::createBadResponse( m_message, code );
}

std::shared_ptr< ErrorInfo > ErrorInfo::error()
{
	return this->shared_from_this();
}
} // namespace idhan
