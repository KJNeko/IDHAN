#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "db/dbTypes.hpp"
#include "queryTerms.hpp"
#include "queryVector.hpp"
#include "threading/ExpectedTask.hpp"

namespace idhan::modules
{
class RemoteModule;
}

namespace idhan::embeddings
{

//! Parses pgvector's "[1,-0.5,...]" text output back into floats.
[[nodiscard]] std::vector< float > parseHalfvecLiteral( const std::string& literal );

//! Resolves every term to a unit vector, in the order the terms were given.
/** Record terms resolve in a single query for all of them; a round trip per reference image would be
 *  a lot of latency for what is one index scan. Text terms are embedded one at a time, which is what
 *  lets a failure name the phrase that caused it.
 *
 *  \param terms Taken by value deliberately: drogon::Task suspends at initial_suspend, so a reference
 *         parameter can outlive its referent. Parameters are copied into the coroutine frame.
 *  \param module Only needed for text terms, and may be null when there are none. The caller is
 *         responsible for having checked supportsText(). */
[[nodiscard]] ExpectedTask< std::vector< WeightedVector > > resolveTerms(
	std::shared_ptr< modules::RemoteModule > module,
	std::int32_t model_id,
	std::vector< QueryTerm > terms,
	DbClientPtr db );

} // namespace idhan::embeddings
