#pragma once

#include "ClusterAccess.hpp"
#include "IDHANTypes.hpp"
#include "idhan/runtime/Task.hpp"

namespace idhan::db
{
class DBContext;
}

namespace idhan::filesystem
{
// Ordinary access never recreates markers. Boot recovery requires a consumed acknowledgement.
[[nodiscard]] runtime::Task< std::expected< ClusterAccess, std::string > > openCluster(
	db::DBContext& db,
	ClusterID id );
[[nodiscard]] runtime::Task< std::expected< std::optional< ClusterAccess >, std::string > > openRecordCluster(
	db::DBContext& db,
	RecordID id );
} // namespace idhan::filesystem
