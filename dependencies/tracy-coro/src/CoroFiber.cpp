//
// The ONE translation unit that includes <tracy/Tracy.hpp>. It defines the fiberEnter/fiberLeave
// forwarding shims declared in idhan_tracy/CoroFiber.hpp, so that header (pulled in almost
// everywhere via the drogon coroutine shim) stays free of Tracy's heavy client headers and their
// macro collisions. Compiled into the idhan_tracy_coro static library.
//
#ifdef TRACY_ENABLE

	#include <tracy/Tracy.hpp>

	#include "idhan_tracy/CoroFiber.hpp"

namespace idhan::tracy_coro
{

void fiberEnter( const char* const name ) noexcept
{
	TracyFiberEnter( name );
}

void fiberLeave() noexcept
{
	TracyFiberLeave;
}

} // namespace idhan::tracy_coro

#endif // TRACY_ENABLE
