#pragma once
#include <folly/io/IOBuf.h>

#include <expected>
#include <simdjson.h>

#include "HTTPConfig.hpp"
#include "ValidatedJson.hpp"

namespace idhan::http
{
class JsonSerializer
{
	simdjson::builder::string_builder m_builder { json_initial_capacity };

  public:

	JsonSerializer();
	~JsonSerializer() = default;

	template < typename T >
	void append( const T& value )
	{
		m_builder.append( value );
	}

	void appendValidated( const ValidatedJson& );
	void addHydrusVersion();
	[[nodiscard]] std::unique_ptr< folly::IOBuf > finish();
};

template < typename T >
[[nodiscard]] std::expected< T, ParseError > parseJson( const ValidatedJson& );
} // namespace idhan::http
