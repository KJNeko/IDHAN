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

//! Record terms resolve in one query; text terms resolve individually so failures can name the phrase.
//! terms is by value because drogon::Task suspends before reference parameters are safe to use.
[[nodiscard]] ExpectedTask< std::vector< WeightedVector > > resolveTerms(
	std::shared_ptr< modules::RemoteModule > module,
	std::int32_t model_id,
	std::vector< QueryTerm > terms,
	DbClientPtr db );

} // namespace idhan::embeddings
