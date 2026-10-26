#pragma once

#include <cstdint>
#include <vector>

#include "IDHANTypes.hpp"
#include "idhan/runtime/Task.hpp"

namespace idhan::db
{
class DBContext;

struct TagDomainCounts
{
	TagDomainID domain_id;
	std::int64_t storage_count;
	std::int64_t display_count;
};

// Empty selection means all domains. Unused domains are returned with zero counts,
// ordered by domain ID. Callers validate requested domain existence separately.
[[nodiscard]] runtime::Task< std::vector< TagDomainCounts > > tagDomainCounts(
	DBContext& db,
	TagID tag_id,
	std::vector< TagDomainID > domains );
} // namespace idhan::db
