#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "IDHANTypes.hpp"
#include "db/dbTypes.hpp"
#include "queryTerms.hpp"
#include "threading/ExpectedTask.hpp"

namespace idhan::modules
{
class RemoteModule;
}

namespace idhan::embeddings
{

//! Every term scored against every record, plus how far the first two records sit from each other.
struct CompareResult
{
	//! [term][record]: outer index follows the terms given, inner index follows the record ids given.
	std::vector< std::vector< double > > m_distances {};
	//! Between the first two records. Absent when fewer than two were asked for.
	std::optional< double > m_pair_distance {};
};

//! Scores each term against each record independently.
/** Distances come from pgvector's <=> rather than from arithmetic here, so this and search can never
 *  disagree about how far apart two things are.
 *
 *  Term weights are deliberately ignored. Each term is scored alone, and cosine distance is
 *  scale-invariant in the query vector, so a weight could not change any number returned.
 *
 *  Unlike search this touches no HNSW index and needs no ef_search: it is a primary-key fetch of a
 *  handful of rows followed by one dot product per (term, record) pair. */
[[nodiscard]] ExpectedTask< CompareResult > compareEmbeddings(
	std::shared_ptr< modules::RemoteModule > module,
	std::int32_t model_id,
	std::size_t dimensions,
	std::vector< QueryTerm > terms,
	std::vector< RecordID > record_ids,
	DbClientPtr db );

} // namespace idhan::embeddings
