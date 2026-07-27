//
// Created by kj16609 on 7/27/26.
//

#include <gtest/gtest.h>

#include <coroutine>
#include <vector>

#include "coro/Resumer.hpp"

namespace
{

class RecordingResumer final : public idhan::coro::Resumer
{
  public:

	std::vector< std::coroutine_handle<> > m_seen {};

	void resume( const std::coroutine_handle<> handle ) noexcept override { m_seen.push_back( handle ); }
};

RecordingResumer* g_resumer { nullptr };

idhan::coro::Resumer* provider() noexcept
{
	return g_resumer;
}

} // namespace

TEST( CoroResumer, noProviderInstalledYieldsNull )
{
	idhan::coro::setResumerProvider( nullptr );
	EXPECT_EQ( idhan::coro::currentResumer(), nullptr );
}

TEST( CoroResumer, installedProviderIsReturned )
{
	RecordingResumer resumer {};
	g_resumer = &resumer;
	idhan::coro::setResumerProvider( &provider );

	EXPECT_EQ( idhan::coro::currentResumer(), &resumer );

	idhan::coro::setResumerProvider( nullptr );
	g_resumer = nullptr;
}

TEST( CoroResumer, resumeIsForwardedToTheResumer )
{
	RecordingResumer resumer {};
	g_resumer = &resumer;
	idhan::coro::setResumerProvider( &provider );

	// Typed as coroutine_handle<> rather than noop_coroutine_handle so the EXPECT_EQ below compares
	// like with like.
	const std::coroutine_handle<> handle { std::noop_coroutine() };
	idhan::coro::currentResumer()->resume( handle );

	ASSERT_EQ( resumer.m_seen.size(), 1u );
	EXPECT_EQ( resumer.m_seen.front(), handle );

	idhan::coro::setResumerProvider( nullptr );
	g_resumer = nullptr;
}
