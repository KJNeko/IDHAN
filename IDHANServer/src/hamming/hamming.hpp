#pragma once

#include <cstddef>
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
// One atomic batch. Returns the highest claimed ID, or empty when no work was
// available in the requested range. Concurrent requeues remain pending.
[[nodiscard]] runtime::Task< std::optional< RecordID > > processQueueBatch(
	db::DBContext& db,
	RecordID through,
	std::optional< RecordID > after = {} );
// Drains the bounded startup/timer snapshot, checking for shutdown between batches.
[[nodiscard]] runtime::Task<> runQueueSweep( db::DBContext& db, std::stop_token stop = {} );
} // namespace idhan::hamming
