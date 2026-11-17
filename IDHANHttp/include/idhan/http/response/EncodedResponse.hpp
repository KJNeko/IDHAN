#pragma once

#include <cstdint>
#include <utility>
#include <variant>

#include "idhan/http/reflection/AnnotationText.hpp"
#include "idhan/http/response/Response.hpp"

namespace idhan::http
{

//! Owns an already encoded or streaming response without copying or re-encoding
//! its body. Schema, media type, and primary status describe its success contract;
//! file transports may additionally return range and conditional statuses.
template < typename Schema, AnnotationText Media = "application/json", std::uint16_t Status = 200 >
struct EncodedResponse
{
	static_assert( Status >= 200 && Status < 400, "EncodedResponse declares a non-error response" );
	static_assert( !Media.view().empty(), "A response media type must not be empty" );
	using Body = Schema;
	static constexpr auto media_type { Media };
	static constexpr auto status { Status };

	Response response;
};

template < typename Schema, AnnotationText Media = "application/json", std::uint16_t Status = 200 >
using EncodedResult = std::variant< EncodedResponse< Schema, Media, Status >, Response >;

//! Keep transport errors outside the declared success alternative.
template < typename Schema, AnnotationText Media = "application/json", std::uint16_t Status = 200 >
[[nodiscard]] EncodedResult< Schema, Media, Status > encodedResponse( Response response )
{
	if ( response.message.getStatusCode() >= 400 ) return response;

	return EncodedResponse< Schema, Media, Status > { std::move( response ) };
}

} // namespace idhan::http
