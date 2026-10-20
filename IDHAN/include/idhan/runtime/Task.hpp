#pragma once
#include <folly/coro/Task.h>

namespace idhan::runtime
{
// Kept separate from the executor-independent IDHANTask until its call sites migrate.
template < typename T = void >
using Task = folly::coro::Task< T >;
} // namespace idhan::runtime
