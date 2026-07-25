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

// Tracy tracks fiber execution PER REAL OS THREAD: a fiberLeave must occur on the same thread as the
// matching fiberEnter, or Tracy aborts with "Fiber execution stopped on a thread which is not
// executing a fiber". Our coroutine hooks emit enter-on-resume / leave-on-suspend, which pairs
// cleanly for sequential coroutines — but drogon's when_all spawns internal AsyncTask coroutines
// that have no await_transform, so they transfer into (and migrate) fiber-instrumented coroutines
// across the thread pool without our bracketing. The result is enters without a preceding leave and
// leaves with no active fiber on the thread.
//
// So we enforce Tracy's invariant here, per thread: `t_active` mirrors Tracy's `td->fiber`. We never
// emit a leave when the thread has no active fiber, and when a new fiber begins while one is still
// active we end the previous one first. Concurrent (when_all) timelines become approximate — a
// fiber's slice may be cut short when the thread switches to another fiber — but every enter/leave
// Tracy sees is valid, so it never aborts.
namespace
{
thread_local const char* t_active { nullptr };
} // namespace

void fiberEnter( const char* const name ) noexcept
{
	if ( t_active == name ) return; // already executing this fiber on this thread

	if ( t_active != nullptr ) TracyFiberLeave; // end the previous fiber's slice on this thread first

	t_active = name;
	TracyFiberEnter( name );
}

void fiberLeave() noexcept
{
	if ( t_active == nullptr ) return; // this thread is not executing a fiber (its enter happened elsewhere)

	t_active = nullptr;
	TracyFiberLeave;
}

} // namespace idhan::tracy_coro

#endif // TRACY_ENABLE
