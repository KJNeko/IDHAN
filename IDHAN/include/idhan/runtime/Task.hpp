#pragma once
#include <folly/coro/Task.h>

namespace idhan::runtime
{
template < typename T = void >
using Task = folly::coro::Task< T >;
} // namespace idhan::runtime
