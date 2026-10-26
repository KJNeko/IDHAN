#pragma once

#include <optional>
#include <string_view>

#include "IDHANTypes.hpp"
#include "idhan/runtime/Task.hpp"

namespace idhan::db
{
class DBTransaction;

[[nodiscard]] runtime::Task< std::optional< ClusterID > > findClusterByName(
	DBTransaction& transaction,
	std::string_view name );
} // namespace idhan::db
