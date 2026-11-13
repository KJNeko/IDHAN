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
class PhashIndex;

inline constexpr std::size_t queueBatchSize { 32 };
//! Matches the CHECK constraint on hamming_distance.distance.
inline constexpr std::uint8_t maxStoredDistance { 8 };

struct QueueBatchResult
{
	RecordID last_record_id;
	std::size_t processed_records;
};

//! One atomic batch. Returns its last ID and committed record count, or empty when no work was
//! available in the requested range. Concurrent requeues remain pending. Claimed records' current
//! hashes are written into `index` before comparing them against it.
[[nodiscard]] runtime::Task< std::optional< QueueBatchResult > > processQueueBatch(
	db::DBContext& db,
	PhashIndex& index,
	RecordID through,
	std::optional< RecordID > after = {} );
//! Drains the bounded startup/timer snapshot, checking for shutdown between batches. Loads every
//! stored hash once per sweep; a hash changed after that load is requeued and corrected later.
[[nodiscard]] runtime::Task< std::uint64_t > runQueueSweep( db::DBContext& db, std::stop_token stop = {} );
} // namespace idhan::hamming
