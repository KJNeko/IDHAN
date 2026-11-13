#pragma once

#include <cstdint>
#include <string_view>

#include "idhan/http/reflection/RequestReflection.hpp"

namespace idhan::http
{

struct Summary
{
	AnnotationText summary;
};

struct Description
{
	AnnotationText description;
};

struct Group
{
	AnnotationText name;
};

struct OperationId
{
	AnnotationText operation_id;
};

struct SchemaName
{
	AnnotationText schema_name;
};

//! Declares an additional error response. Non-error schemas, statuses, and media
//! types are inferred from JsonResponse, EmptyResponse, and EncodedResponse returns.
template < typename T = JsonDocument >
struct Output
{
	using Body = T;
	std::uint16_t response_status;
	AnnotationText media_type;

	consteval Output( const std::uint16_t status = 200, const std::string_view media = "application/json" ) :
	  response_status { status },
	  media_type { media }
	{
		if ( status < 200 || status > 599 ) throw "An output must have a final HTTP status";

		if constexpr ( !std::same_as< T, void > )
		{
			if ( status == 204 || status == 205 || status == 304 ) throw "Use Output<void> for a bodyless status";
		}
		if ( media.empty() ) throw "Output media type must not be empty";
	}
};

} // namespace idhan::http
