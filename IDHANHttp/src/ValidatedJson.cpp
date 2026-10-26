#include "idhan/http/ValidatedJson.hpp"

#include <simdjson.h>

namespace idhan::http
{

ValidatedJson::ValidatedJson( std::string text ) : m_text( std::move( text ) )
{}

std::expected< ValidatedJson, ParseError > ValidatedJson::parse( std::string text )
{
	simdjson::dom::parser parser;
	simdjson::padded_string input { text };
	const auto document { parser.parse( input ) };
	if ( document.error() )
		return std::unexpected( ParseError { "body", simdjson::error_message( document.error() ) } );

	return ValidatedJson { std::move( text ) };
}

std::string_view ValidatedJson::text() const noexcept
{
	return m_text;
}

} // namespace idhan::http
