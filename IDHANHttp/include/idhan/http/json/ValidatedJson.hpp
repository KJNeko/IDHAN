#pragma once
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace idhan::http
{
struct ParseError
{
	std::string field, message;
	std::uint16_t status { 400 };
};

class ValidatedJson
{
	std::string m_text;
	explicit ValidatedJson( std::string );

  public:

	[[nodiscard]] static std::expected< ValidatedJson, ParseError > parse( std::string );
	[[nodiscard]] std::string_view text() const noexcept;
};
} // namespace idhan::http
