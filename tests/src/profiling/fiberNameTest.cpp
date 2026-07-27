#include <gtest/gtest.h>

#include "idhan_tracy/CoroFiber.hpp"

using namespace idhan::tracy_coro;

TEST( FiberName, IdsAreUniqueAndMonotonic )
{
	const auto a { nextFiberId() };
	const auto b { nextFiberId() };
	EXPECT_NE( a, b );
	EXPECT_LT( a, b );
}

TEST( FiberName, PrefixWithoutTag )
{
	currentFiberTag() = nullptr;
	const auto name { makeFiberName( "idhan" ) };
	EXPECT_TRUE( name.starts_with( "idhan #" ) );
}

TEST( FiberName, PrefixWithTag )
{
	currentFiberTag() = "GET /search";
	const auto name { makeFiberName( "idhan" ) };
	EXPECT_NE( name.find( "GET /search" ), std::string::npos );
	EXPECT_TRUE( name.starts_with( "idhan GET /search #" ) );
	currentFiberTag() = nullptr; // reset for other tests
}
