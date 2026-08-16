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

struct SearchHit
{
	RecordID m_record_id { 0 };
	double m_distance { 0.0 };
};

//! Record-only queries never touch the module system.
[[nodiscard]] ExpectedTask< std::vector< SearchHit > > searchEmbeddings(
	std::shared_ptr< modules::RemoteModule > module,
	std::int32_t model_id,
	std::size_t dimensions,
	std::vector< QueryTerm > terms,
	std::size_t limit,
	std::size_t ef_search,
	DbClientPtr db );

} // namespace idhan::embeddings
