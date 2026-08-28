#pragma once

#include <chrono>
#include <cstdint>

#include "db/dbTypes.hpp"
#include "drogon/utils/coroutine.h"

namespace idhan::hamming
{

//! Queue rows claimed by a single sweep.
constexpr std::int32_t QUEUE_BATCH_SIZE { 32 };

//! Pairs further apart than this are discarded. Mirrored by hamming_distance's CHECK.
constexpr std::int32_t MAX_DISTANCE { 8 };

constexpr std::chrono::seconds QUEUE_INTERVAL { 1 };

//! Claims up to QUEUE_BATCH_SIZE queued records and stores their distances to every other hashed record.
drogon::Task< void > processQueueBatch( DbClientPtr db );

//! Registers the recurring sweep on the app's main loop.
void startQueueSweeper();

} // namespace idhan::hamming
