#include <charconv>

#include "api/helpers/createBadRequest.hpp"
#include "api/helpers/helpers.hpp"

namespace idhan::api::helpers
{

std::expected< std::uint16_t, drogon::HttpResponsePtr > parseBoundedParameter(
	const std::optional< std::string >& parameter,
	const std::string_view name,
	const std::uint16_t fallback,
	const std::uint16_t maximum )
{
	if ( !parameter ) return fallback;

	std::uint16_t value { 0 };
	const auto& text { *parameter };
	const auto [ end, error ] { std::from_chars( text.data(), text.data() + text.size(), value ) };

	if ( error != std::errc {} || end != text.data() + text.size() )
		return std::unexpected( createBadRequest( "`{}` must be an unsigned integer, got `{}`", name, text ) );

	if ( value > maximum )
		return std::unexpected( createBadRequest( "`{}` must be no greater than {}, got {}", name, maximum, value ) );

	return value;
}

} // namespace idhan::api::helpers
