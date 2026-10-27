#pragma once

#include <proxygen/lib/http/HTTPMethod.h>

#include <optional>
#include <simdjson.h>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "RequestParameters.hpp"

namespace idhan::http
{

enum class UnknownParameterPolicy
{
	warn,
	reject
};

struct JsonParameterObject
{
	// Empty selects the root object. A "*" segment traverses array elements;
	// undeclared object paths (including free-form maps) are not inspected.
	std::vector< std::string > path;
	std::vector< std::string > names;
};

struct ParameterRules
{
	std::vector< std::string > query {};
	std::vector< JsonParameterObject > json {};
	UnknownParameterPolicy unknown { UnknownParameterPolicy::warn };
};

[[nodiscard]] std::vector< std::string > unknownQueryParameters(
	const RequestParameters& parameters,
	std::span< const std::string > allowed );

[[nodiscard]] std::vector< std::string > unknownJsonParameters(
	simdjson::dom::element document,
	std::span< const JsonParameterObject > declarations );

// Returns a rejection message only in reject mode. Diagnostics contain names,
// never submitted values, and are escaped so control characters stay on one line.
[[nodiscard]] std::optional< std::string > handleUnknownParameters(
	std::span< const std::string > names,
	std::string_view location,
	proxygen::HTTPMethod method,
	std::string_view path,
	UnknownParameterPolicy policy );

} // namespace idhan::http
