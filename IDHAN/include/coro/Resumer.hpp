#pragma once

#include <coroutine>

#include "fgl/defines.hpp"

namespace idhan::coro
{

//! Hands a coroutine handle back to whatever loop owns it. Captured on the suspending thread,
//! invoked on the io completion thread, so implementations must be safe to call from another thread.
class Resumer
{
  public:

	Resumer() = default;
	virtual ~Resumer() = default;

	FGL_DELETE_COPY( Resumer );
	FGL_DELETE_MOVE( Resumer );

	//! Called on the io completion thread. Must not run the coroutine body inline unless the
	//! implementation genuinely has nowhere else to put it.
	virtual void resume( std::coroutine_handle<> handle ) noexcept = 0;
};

//! Returns the Resumer that a coroutine suspending on the CALLING thread must be resumed through.
using ResumerProvider = Resumer* (*)() noexcept;

//! Installs the process-wide provider. Call once at startup, before any io is submitted. Passing
//! nullptr uninstalls it, which is only useful in tests.
void setResumerProvider( ResumerProvider provider ) noexcept;

//! Called by awaiters on the suspending thread. Returns nullptr when no provider is installed, or
//! when the installed provider has nothing for this thread.
[[nodiscard]] Resumer* currentResumer() noexcept;

} // namespace idhan::coro
