#pragma once
#ifndef MRMIME_BYTE_SIGNATURE_HPP_INCLUDED
#define MRMIME_BYTE_SIGNATURE_HPP_INCLUDED

#include <cstddef> // std::size_t
#include <tuple>
#include <cassert>

#include "filetype_enum.h"
#include "skipbytes.hpp"
#include "matchbytes.hpp"

namespace MrMime {
namespace internal {

/*** TODO
Tuple concepts: expects tuple<array<byte>>,
*/

template <class T>
concept StdArrayOfBytes = requires
{
	requires std::same_as<
		std::byte,
		std::remove_cv_t<typename T::value_type>>;

	requires std::same_as<
		std::array<typename T::value_type, sizeof(T)>,
		std::remove_cv_t<T>>;
};

template <class T>
concept StdArrayOfConstBytes = requires
{
	requires StdArrayOfBytes<T>;
	requires std::is_const_v<typename T::value_type>;
};

template<typename TUPLE_T> // expects tuple<array<byte>>, TODO concept
struct Byte_Signature
{
	const FileType m_id;
	const TUPLE_T m_sig;

	[[nodiscard]]
	explicit constexpr Byte_Signature(const FileType ft, const TUPLE_T sig)
	: m_id(ft), m_sig(sig)
	{}

	/// Returns the size in bytes of the signature
	[[nodiscard]] constexpr std::size_t size() const noexcept
	{
		return std::apply(
			[]<typename ... ELEMENTS>(const ELEMENTS& ... elements)
			constexpr noexcept -> std::size_t
			{
				return (elements.size() + ...);
			},
			m_sig
		);
	}

	/// Doesn't perform bounds-checking; onus is on caller!
	// Undefined behavior if size of ARRAY_T is less than size()
	template<StdArrayOfBytes ARRAY_T> [[nodiscard]]
	bool compare(FileType& ft_out, const ARRAY_T& arr) const
	{
		// this could be done with more info about TUPLE_T's elements
		// static_assert(sizeof(ARRAY_T) >= size());
		const bool arr_matches_sig{
			std::apply(
				[&]<typename ... ELEMENTS>(const ELEMENTS& ... elements)
				-> bool
				{
					auto cursor{ arr.cbegin() };
					return (elements.matches(cursor) && ...);
				},
				m_sig
			)
		};

		if (arr_matches_sig)
			ft_out = m_id;

		return arr_matches_sig;
	}

	// MatchBytes

	template <std::size_t LEN>
	[[nodiscard]] constexpr auto operator<<(const char (&cstr)[LEN]) const
	{ return BS_Factory(std::tuple_cat(m_sig, std::tuple(MatchBytes(cstr)))); }

	template <std::size_t LEN>
	[[nodiscard]] constexpr auto operator<<(MatchBytes<LEN> mb) const
	{ return BS_Factory(std::tuple_cat(m_sig, std::tuple(mb))); }

	[[nodiscard]] constexpr auto operator<<(const SkipBytes::SizeType s) const
	{ return BS_Factory(std::tuple_cat(m_sig, std::tuple(SkipBytes(s)))); }

	[[nodiscard]] constexpr auto operator<<(const SkipBytes sb) const
	{ return BS_Factory(std::tuple_cat(m_sig, std::tuple(sb))); }

private:

	template<typename TUPLE_T_but_different> [[nodiscard]] // TODO tup concept
	constexpr inline auto BS_Factory(const TUPLE_T_but_different t) const
	{ return Byte_Signature<TUPLE_T_but_different>(m_id, t); }
};

/// A factory object used as the initial component of a signature stream
struct Byte_Signature_Stream_Starter
{ // TODO; is it possible to just get rid of this? empty tuple?
	const FileType m_id;

	[[nodiscard]]
	explicit constexpr Byte_Signature_Stream_Starter(const FileType ft)
	: m_id(ft)
	{}

	// MatchBytes

	template<std::size_t LEN>
	[[nodiscard]] constexpr auto operator<<(const char (&cstr)[LEN]) const
	{ return Byte_Signature(m_id, std::tuple(MatchBytes(cstr))); }

	template<std::size_t LEN>
	[[nodiscard]] constexpr auto operator<<(MatchBytes<LEN> mb) const
	{ return Byte_Signature(m_id, std::tuple(mb)); }

	// SkipBytes

	//[[nodiscard]] constexpr auto operator<<(const std::size_t s) const
	//{ return Byte_Signature(m_id, std::tuple(SkipBytes(s))); }

	[[nodiscard]] constexpr auto operator<<(const SkipBytes sb) const
	{ return Byte_Signature(m_id, std::tuple(sb)); }
};

} // namespace internal
} // namespace MrMime

#endif // MRMIME_BYTE_SIGNATURE_HPP_INCLUDED
