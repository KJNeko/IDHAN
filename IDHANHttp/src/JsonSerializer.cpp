#include "idhan/http/JsonSerializer.hpp"

#include <stdexcept>

namespace idhan::http
{
JsonSerializer::JsonSerializer() = default;

void JsonSerializer::appendValidated( const ValidatedJson& json )
{
	m_builder.append_raw( json.text() );
}

void JsonSerializer::addHydrusVersion()
{
	m_builder.append_key_value( "version", 1 );
}

std::unique_ptr< folly::IOBuf > JsonSerializer::finish()
{
	auto json { m_builder.view() };
	if ( json.error() ) throw std::runtime_error( simdjson::error_message( json.error() ) );

	return folly::IOBuf::copyBuffer( *json );
}
} // namespace idhan::http
