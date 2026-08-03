#pragma once

#include <cstddef>
#include <functional>

namespace idhan::hydrus::ptr
{

//! Worker threads a stage uses when it is asked for 0: every reported hardware thread, at least one.
unsigned defaultWorkerCount();

//! Runs \p body( index ) once for every index in [0, \p count), across at most \p threads workers
//! (0 means defaultWorkerCount(), and never more workers than there is work). Indices are claimed
//! atomically, so a slow item stalls no other lane.
//!
//! \p body returns false to stop the run: indices not yet claimed are never started. Bodies for
//! different indices run concurrently, and must synchronise their own access to anything shared.
//!
//! \return false if some body asked to stop, true if every index ran.
//!
//! \throws Whatever the first failing body threw, rethrown on the calling thread once every worker
//!         has joined. This is the main reason the pool lives here rather than being open-coded per
//!         stage: an exception escaping a std::jthread callable calls std::terminate, which would
//!         turn "the disk filled up four hours in" from a reported failure into a crash.
bool parallelIndexed( std::size_t count, unsigned threads, const std::function< bool( std::size_t ) >& body );

} // namespace idhan::hydrus::ptr
