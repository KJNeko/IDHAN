#include <cstddef> // size_t, byte
#include <array>
#include "skipbytes.hpp"

using namespace MrMime::internal;

/// SkipBytes Unit Test
static_assert(
	[]() consteval -> bool
	{
		const auto h00{ static_cast<std::byte>(0x00) };
		const auto hFF{ static_cast<std::byte>(0xFF) };
		constexpr std::array arr1{ h00, hFF, h00, hFF};
		constexpr std::array arr2{ hFF, hFF, hFF, hFF , hFF};

		constexpr SkipBytes sb(arr1.size());

		static_assert(sb.bytes_to_skip() == arr1.size());
		static_assert(sb.bytes_to_skip() == sb.size());

		// test if matches() correctly
		static_assert(sb.matches(arr1.cbegin()));
		static_assert(sb.matches(arr2.cbegin()));

		// assert matches() updates the cursor correctly
		static_assert(
			[&sb, &arr1]() consteval -> bool
			{
				auto cursor{ arr1.begin() };
				(void) sb.matches(cursor);
				return cursor == arr1.end();
			}()
		);

		return true;
	}()
);
