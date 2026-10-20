#pragma once

#include <stop_token>

#include "idhan/runtime/Task.hpp"

namespace idhan::db
{
class DBContext;
}

namespace idhan::metadata
{
// Visits a bounded snapshot in batches of eight. Failed or blank images remain
// eligible for future passes, without starving later IDs in this pass.
[[nodiscard]] runtime::Task<> runPhashSweep( db::DBContext& db, std::stop_token stop = {} );
} // namespace idhan::metadata
