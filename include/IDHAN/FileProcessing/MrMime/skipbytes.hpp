#pragma once
#ifndef MRMIME_SKIPBYTES_HPP_INCLUDED
#define MRMIME_SKIPBYTES_HPP_INCLUDED

#include <cstddef> // size_t, byte
#include <cstdint> // uint8_t
#include <iterator> // forward_iterator, iter_value_t

namespace MrMime {
namespace internal {

/// To be used in a byte signature stream, representing N bytes to skip over
// Acts as a wildcard: SkipBytes(2) will match any two bytes.
class SkipBytes
{
	public: typedef uint8_t SizeType;

	private:
	const SizeType m_bytes{ 0 };

	public:
	[[nodiscard]] constexpr
	SkipBytes(const SizeType number_of_bytes_to_skip) noexcept
	: m_bytes(number_of_bytes_to_skip)
	{}

	/// Doesn't perform bounds-checking; onus is on caller!
	// undefined behavior if cursor + size() - 1 is not within bounds
	template<std::forward_iterator ITER_T>
	requires std::same_as<std::byte, std::iter_value_t<ITER_T>>
	[[nodiscard]] constexpr inline bool matches(ITER_T& cursor) const noexcept
	{
		cursor += bytes_to_skip(); // advance cursor
		return true;
	}

	/// Doesn't perform bounds-checking; onus is on caller!
	// undefined behavior if cursor + size() - 1 is not within bounds
	template<std::forward_iterator ITER_T>
	requires std::same_as<std::byte, std::iter_value_t<ITER_T>>
	[[nodiscard]] inline constexpr
	bool matches(const ITER_T&& cursor) const noexcept
	{
		auto temp_cursor{ cursor };
		return matches(temp_cursor);
	}

	constexpr inline std::size_t bytes_to_skip() const noexcept
	{ return static_cast<std::size_t>(m_bytes); }

	constexpr inline std::size_t size() const noexcept
	{ return static_cast<std::size_t>(m_bytes); }
};

} // namespace internal
} // namespace MrMime

#endif // MRMIME_SKIPBYTES_HPP_INCLUDED
