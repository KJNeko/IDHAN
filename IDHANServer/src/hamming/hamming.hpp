#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stop_token>

#include "IDHANTypes.hpp"
#include "idhan/runtime/Task.hpp"

namespace idhan::db
{
class DBContext;
}

namespace idhan::hamming
{
inline constexpr std::size_t queueBatchSize { 32 };

struct QueueBatchResult
{
	RecordID last_record_id;
	std::size_t processed_records;
};

// One atomic batch. Returns its last ID and committed record count, or empty when no work was
// available in the requested range. Concurrent requeues remain pending.
[[nodiscard]] runtime::Task< std::optional< QueueBatchResult > > processQueueBatch(
	db::DBContext& db,
	RecordID through,
	std::optional< RecordID > after = {} );
// Drains the bounded startup/timer snapshot, checking for shutdown between batches.
[[nodiscard]] runtime::Task< std::uint64_t > runQueueSweep( db::DBContext& db, std::stop_token stop = {} );
} // namespace idhan::hamming
