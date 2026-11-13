#pragma once

#include <coroutine>

#include "fgl/defines.hpp"

namespace idhan::coro
{

//! Captured on the suspending thread and invoked on the I/O completion thread; must be thread-safe.
class Resumer
{
  public:

	Resumer() = default;
	virtual ~Resumer() = default;

	FGL_DELETE_COPY( Resumer );
	FGL_DELETE_MOVE( Resumer );

	//! Resume on the owning loop; run inline only when no loop is available.
	virtual void resume( std::coroutine_handle<> handle ) noexcept = 0;
};

//! Selects the resumer for the calling thread.
using ResumerProvider = Resumer* (*)() noexcept;

//! Install once before submitting I/O; nullptr uninstalls the process-wide provider.
void setResumerProvider( ResumerProvider provider ) noexcept;

//! Called on the suspending thread; returns nullptr if no provider or resumer is available.
[[nodiscard]] Resumer* currentResumer() noexcept;

} // namespace idhan::coro
