#pragma once

#include <cstddef>
#include <functional>

namespace idhan::hydrus::ptr
{

//! Worker threads a stage uses when it is asked for 0: every reported hardware thread, at least one.
unsigned defaultWorkerCount();

//! Runs \p body( index ) once for every index in [0, \p count), across at most \p threads workers
//! (0 means defaultWorkerCount()). \p body returns false to stop the run; unclaimed indices never
//! start. Bodies run concurrently and must synchronise their own shared state.
//!
//! \return false if some body asked to stop, true if every index ran.
//! \throws Whatever the first failing body threw, rethrown once every worker has joined.
bool parallelIndexed( std::size_t count, unsigned threads, const std::function< bool( std::size_t ) >& body );

} // namespace idhan::hydrus::ptr
