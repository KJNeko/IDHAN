#pragma once

#include <cstdint>

#include "idhan/runtime/Task.hpp"

namespace idhan::db
{
class DBContext;

struct TagCountBatch
{
	std::uint64_t deltas {};
	std::uint64_t counters {};
};

// Applies all committed deltas visible to one statement and consumes them atomically.
// Concurrent consumers skip the pass; mapping writers never acquire the consumer lock.
[[nodiscard]] runtime::Task< TagCountBatch > accumulateTagCountDeltas( DBContext& db );
} // namespace idhan::db
