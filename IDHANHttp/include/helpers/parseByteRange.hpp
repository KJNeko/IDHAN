#pragma once

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <string_view>

namespace idhan::http
{

struct ByteRange
{
	std::uint64_t offset;
	std::uint64_t length;
};

enum class ByteRangeError
{
	Invalid,
	Unsatisfiable
};

constexpr std::optional< std::uint64_t > integer( std::string_view text )
{
	if ( text.empty() ) return std::nullopt;

	std::uint64_t value {};
	const auto result { std::from_chars( text.data(), text.data() + text.size(), value ) };
	if ( result.ec != std::errc {} ) return std::nullopt;

	if ( result.ptr != text.data() + text.size() ) return std::nullopt;

	return value;
}

constexpr std::expected< ByteRange, ByteRangeError > parseByteRange(
	const std::string_view header,
	const std::uint64_t size )
{
	if ( !header.starts_with( "bytes=" ) ) return std::unexpected( ByteRangeError::Invalid );

	const auto spec { header.substr( 6 ) };
	const auto dash { spec.find( '-' ) };
	if ( dash == std::string_view::npos ) return std::unexpected( ByteRangeError::Invalid );

	const auto end_text { spec.substr( dash + 1 ) };
	if ( dash == 0 )
	{
		const auto suffix { integer( end_text ) };
		if ( !suffix ) return std::unexpected( ByteRangeError::Invalid );

		if ( *suffix == 0 ) return std::unexpected( ByteRangeError::Unsatisfiable );

		const auto length { std::min( *suffix, size ) };
		return ByteRange { size - length, length };
	}

	const auto first { integer( spec.substr( 0, dash ) ) };
	if ( !first ) return std::unexpected( ByteRangeError::Invalid );

	std::uint64_t last { std::numeric_limits< std::uint64_t >::max() };
	if ( !end_text.empty() )
	{
		const auto parsed { integer( end_text ) };
		if ( !parsed ) return std::unexpected( ByteRangeError::Invalid );

		if ( *parsed < *first ) return std::unexpected( ByteRangeError::Invalid );

		last = *parsed;
	}

	if ( *first >= size ) return std::unexpected( ByteRangeError::Unsatisfiable );

	const auto end { std::min( last, size - 1 ) };
	return ByteRange { *first, end - *first + 1 };
}

} // namespace idhan::http
