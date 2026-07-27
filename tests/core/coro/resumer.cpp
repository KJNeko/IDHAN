//
// Created by kj16609 on 7/27/26.
//

#include <gtest/gtest.h>

#include <coroutine>
#include <vector>

#include "coro/Resumer.hpp"
#include "fgl/defines.hpp"

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

//! Uninstalls the process-wide provider and clears g_resumer on scope exit, regardless of how the
//! scope is left. Without this, a failed ASSERT_EQ returns from the test early and skips manual
//! cleanup, leaving the process-wide provider pointing at a destroyed stack local for later tests in
//! the same binary to read.
class ScopedProvider
{
  public:

	ScopedProvider( RecordingResumer& resumer, idhan::coro::ResumerProvider providerFn ) noexcept
	{
		g_resumer = &resumer;
		idhan::coro::setResumerProvider( providerFn );
	}

	~ScopedProvider()
	{
		idhan::coro::setResumerProvider( nullptr );
		g_resumer = nullptr;
	}

	FGL_DELETE_COPY( ScopedProvider );
	FGL_DELETE_MOVE( ScopedProvider );
};

} // namespace

TEST( CoroResumer, noProviderInstalledYieldsNull )
{
	idhan::coro::setResumerProvider( nullptr );
	EXPECT_EQ( idhan::coro::currentResumer(), nullptr );
}

TEST( CoroResumer, installedProviderIsReturned )
{
	RecordingResumer resumer {};
	ScopedProvider scope { resumer, &provider };

	EXPECT_EQ( idhan::coro::currentResumer(), &resumer );
}

TEST( CoroResumer, resumeIsForwardedToTheResumer )
{
	RecordingResumer resumer {};
	ScopedProvider scope { resumer, &provider };

	// Typed as coroutine_handle<> rather than noop_coroutine_handle so the EXPECT_EQ below compares
	// like with like.
	const std::coroutine_handle<> handle { std::noop_coroutine() };
	idhan::coro::currentResumer()->resume( handle );

	ASSERT_EQ( resumer.m_seen.size(), 1u );
	EXPECT_EQ( resumer.m_seen.front(), handle );
}
