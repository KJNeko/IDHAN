#pragma once

#include <cstdint>
#include <memory>
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

//! One result, in distance order.
struct SearchHit
{
	RecordID m_record_id { 0 };
	double m_distance { 0.0 };
};

//! Resolves \p terms to vectors, sums them by weight, and returns the nearest records.
/** Record terms resolve in a single query for all of them; a round trip per reference image would be
 *  a lot of latency for what is one index scan.
 *
 *  \param module Only needed for text terms, and may be null when the query has none. The caller is
 *         responsible for having checked supportsText(). A record-only query never touches the
 *         module system at all. */
[[nodiscard]] ExpectedTask< std::vector< SearchHit > > searchEmbeddings(
	std::shared_ptr< modules::RemoteModule > module,
	std::int32_t model_id,
	std::size_t dimensions,
	std::vector< QueryTerm > terms,
	std::size_t limit,
	std::size_t ef_search,
	DbClientPtr db );

} // namespace idhan::embeddings
