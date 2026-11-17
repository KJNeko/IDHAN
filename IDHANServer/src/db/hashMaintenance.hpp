#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "IDHANTypes.hpp"
#include "PerceptualHash.hpp"
#include "hamming/PhashIndex.hpp"
#include "idhan/SHA256.hpp"
#include "idhan/runtime/Task.hpp"

namespace idhan::db
{
class DBContext;
class DBTransaction;

struct RecordPhash
{
	RecordID record_id;
	std::optional< std::uint64_t > phash;
};

struct MissingPhashRecord
{
	RecordID record_id;
	ClusterID cluster_id;
	std::optional< MimeID > mime_id;
	SHA256 sha256;
	std::string extension;
};

[[nodiscard]] runtime::Task< std::optional< RecordID > > queuedHammingHighWater( DBContext& db );
//! The caller acquires the relationship advisory lock before claiming queue rows.
//! Deletion is transactional: rollback restores work, while concurrent requeues
//! wait for completion and remain pending after it commits.
[[nodiscard]] runtime::Task< std::vector< RecordID > > claimHammingBatch(
	DBTransaction& transaction,
	std::optional< RecordID > after,
	RecordID through,
	std::size_t limit );
[[nodiscard]] runtime::Task< hamming::PhashIndex > loadPhashIndex( DBContext& db );
//! One entry per requested record, without a hash when it has none.
[[nodiscard]] runtime::Task< std::vector< RecordPhash > > currentPhashes(
	DBTransaction& transaction,
	std::vector< RecordID > records );
//! Drops every pair touching `records`, then stores the `pairs` whose duplicate groups are still undecided.
[[nodiscard]] runtime::Task< std::int64_t > replaceHammingPairs(
	DBTransaction& transaction,
	std::vector< RecordID > records,
	std::vector< hamming::HammingPair > pairs );

[[nodiscard]] runtime::Task< std::optional< RecordID > > missingPhashHighWater(
	DBContext& db,
	std::vector< MimeID > phash_mimes );
[[nodiscard]] runtime::Task< std::vector< MissingPhashRecord > > missingPhashBatch(
	DBContext& db,
	std::optional< RecordID > after,
	RecordID through,
	std::size_t limit,
	std::vector< MimeID > phash_mimes );
//! Updates only a still-missing hash on the same active file assignment. It never
//! overwrites a foreground parser's result or other metadata fields.
[[nodiscard]] runtime::Task< bool > updateMissingPhash(
	DBContext& db,
	const MissingPhashRecord& record,
	const PerceptualHash& hash );
} // namespace idhan::db
