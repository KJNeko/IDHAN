#pragma once

#include <chrono>
#include <cstdint>
#include <vector>

#include "PerceptualHash.hpp"
#include "db/dbTypes.hpp"
#include "drogon/utils/coroutine.h"

namespace idhan::metadata
{

constexpr std::int32_t PHASH_BATCH_SIZE { 8 };
constexpr std::chrono::seconds PHASH_INTERVAL { 1 };

drogon::Task< std::vector< RecordID > > findMissingPhashes( DbClientPtr db );
drogon::Task< std::vector< RecordID > > findMissingPhashes( DbClientPtr db, std::vector< RecordID > candidates );

// Counts only stored, undeleted records eligible for the sweep.
drogon::Task< std::int64_t > countMissingPhashes( DbClientPtr db );

// Preserves a hash written by another parser while the background parse was running.
drogon::Task< void > updateMissingPhash( RecordID record_id, DbClientPtr db, PerceptualHash phash );

// Drains a snapshot in batches; overlapping calls return immediately.
drogon::Task< void > runPhashSweep( DbClientPtr db );

void startPhashSweeper();

} // namespace idhan::metadata
