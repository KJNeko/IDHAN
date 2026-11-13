#pragma once

#include <json/writer.h>

#include <expected>
#include <simdjson.h>
#include <string>

#include "idhan/http/reflection/RequestReflection.hpp"

namespace idhan::httpapi
{

//! The downloader still exposes script metadata through JsonCpp. Decode it into
//! the HTTP contract here so serialization and documentation use the same type.
template < typename T >
[[nodiscard]] std::expected< T, std::string > decodeDownloaderJson( const Json::Value& value )
{
	Json::StreamWriterBuilder writer;
	writer[ "indentation" ] = "";
	const auto text { Json::writeString( writer, value ) };
	simdjson::dom::parser parser;
	simdjson::dom::element document;
	const auto error { parser.parse( text ).get( document ) };
	if ( error != simdjson::SUCCESS ) return std::unexpected( std::string { simdjson::error_message( error ) } );

	auto result_e { http::decodeJson< T >( document ) };
	if ( !result_e.has_value() ) return std::unexpected( std::move( result_e.error().message ) );

	return std::move( result_e.value() );
}

} // namespace idhan::httpapi
