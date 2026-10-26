#pragma once

#include <string>

#include "IDHANTypes.hpp"
#include "idhan/runtime/Task.hpp"

namespace idhan::db
{
class DBTransaction;

// Serializes missing-domain creation because insertion creates tag-table partitions.
[[nodiscard]] runtime::Task< TagDomainID > getOrCreateTagDomain( DBTransaction& transaction, std::string name );

} // namespace idhan::db
