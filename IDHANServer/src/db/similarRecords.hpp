#pragma once

#include <bitset>
#include <optional>

#include "IDHANTypes.hpp"
#include "idhan/db/DBResult.hpp"
#include "idhan/runtime/Task.hpp"

namespace idhan::db
{
class DBTransaction;

struct SimilarityProbe
{
	std::bitset< 64 > phash;
	RecordID king_id;
};

[[nodiscard]] runtime::Task< std::optional< SimilarityProbe > > similarityProbe(
	DBTransaction& transaction,
	RecordID record_id );
[[nodiscard]] runtime::Task< DBRows< RecordID, RecordID > > alternativeKingPairs(
	DBTransaction& transaction,
	RecordID king_id );
[[nodiscard]] runtime::Task< DBRows< RecordID, RecordID > > unrelatedKingPairs(
	DBTransaction& transaction,
	RecordID king_id );
//! Returns all matches ordered by distance, then record ID. Callers apply relationship filters before limiting results.
[[nodiscard]] runtime::Task< DBRows< RecordID, Integer, RecordID > > similarityCandidates(
	DBTransaction& transaction,
	std::bitset< 64 > phash,
	RecordID record_id,
	Integer distance );
} // namespace idhan::db
