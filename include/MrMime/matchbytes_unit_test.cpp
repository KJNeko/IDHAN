#include "matchbytes.hpp"

using namespace MrMime::internal;

/// MatchBytes Unit Test
static_assert(
	[]() consteval -> bool
	{
		const auto mb_0{ static_cast<std::byte>(0x10) };
		const auto mb_1{ static_cast<std::byte>(0xFF) };
		const auto mb_2{ static_cast<std::byte>('Z') };
		constexpr std::array arr_succ{ mb_0, mb_1, mb_2, mb_0 };
		constexpr std::array arr_fail{ mb_0, mb_2, mb_1, mb_0 };

		constexpr MatchBytes mb("\x10\xFFZ");

		static_assert(mb.size() == 3);
		static_assert(mb[0] == mb_0);
		static_assert(mb[1] == mb_1);
		static_assert(mb[2] == mb_2);

		static_assert(*(mb.cbegin()) == mb_0);
		static_assert(*(mb.cend() - 1) == mb_2);

		// assert matches() correctly
		static_assert(mb.matches(arr_succ.cbegin()));
		static_assert(!mb.matches(arr_fail.cbegin()));

		// assert matches() updates the cursor correctly
		static_assert(
			[&mb, &arr_succ]() consteval -> bool
			{
				auto cursor{ arr_succ.begin() };
				(void) mb.matches(cursor);
				return cursor == arr_succ.begin() + mb.size();
			}()
		);

		return true;
	}()
);
