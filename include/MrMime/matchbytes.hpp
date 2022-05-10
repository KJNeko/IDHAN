#pragma once
#ifndef MRMIME_MATCHBYTES_HPP_INCLUDED
#define MRMIME_MATCHBYTES_HPP_INCLUDED

#include <cstddef> // std::size_t
#include <cstdint> // uint8_t
#include <array>
#include <fgl/utility/zip.hpp>

namespace MrMime
{
	namespace internal
	{

		template < std::size_t LEN >
		class MatchBytes
		{
			std::array<std::byte, LEN - 1> m_bytes;

		public:
			[[nodiscard]] constexpr MatchBytes( const char (& cstr)[LEN] ) noexcept
					: m_bytes{ static_cast<std::byte>( 0 ) }
			{
				for ( const auto&& [ b, c ]: fgl::zip( m_bytes, cstr ))
				{
					b = static_cast<std::byte>(c);
				}
			}

			/// TODO move these matches() methods to a separate dependency?

			/// Doesn't perform bounds-checking; onus is on caller!
			// undefined behavior if cursor + size() - 1 is not within bounds!
			template < std::forward_iterator ITER_T >
			requires std::same_as<std::byte, std::iter_value_t<ITER_T>>
			[[nodiscard]] inline constexpr bool matches( ITER_T& cursor ) const
			{
				const auto cbegin{ cursor };
				const auto cend{ cursor + size() };
				cursor = cend; // advance cursor
				return std::equal( cbegin, cend, m_bytes.cbegin(), m_bytes.cend());
			}

			/// Doesn't perform bounds-checking; onus is on caller!
			// undefined behavior if cursor + size() - 1 is not within bounds!
			template < std::forward_iterator ITER_T >
			requires std::same_as<std::byte, std::iter_value_t<ITER_T>>
			[[nodiscard]] inline constexpr bool matches( const ITER_T&& cursor ) const
			{
				auto temp_cursor{ cursor };
				return matches( temp_cursor );
			}

			[[nodiscard]] constexpr inline std::size_t size() const noexcept
			{
				return LEN - 1;
			}

			[[nodiscard]] constexpr inline std::byte operator[]( const decltype(m_bytes)::size_type i ) const
			{
				return m_bytes.at( i );
			}

			[[nodiscard]] constexpr inline std::byte& operator[]( const decltype(m_bytes)::size_type i )
			{
				return m_bytes.at( i );
			}

			[[nodiscard]] constexpr inline auto begin() noexcept
			{
				return m_bytes.begin();
			}

			[[nodiscard]] constexpr inline auto end() noexcept
			{
				return m_bytes.end();
			}

			[[nodiscard]] constexpr inline auto cbegin() const noexcept
			{
				return m_bytes.cbegin();
			}

			[[nodiscard]] constexpr inline auto cend() const noexcept
			{
				return m_bytes.cend();
			}
		};

		static_assert( std::ranges::random_access_range<MatchBytes<1>> );

	} // namespace internal
} // namespace MrMime

#endif // MRMIME_MATCHBYTES_HPP_INCLUDED
