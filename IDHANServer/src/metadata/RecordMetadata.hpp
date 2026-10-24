#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>

#include "IDHANTypes.hpp"
#include "idhan/runtime/Task.hpp"

namespace idhan::db
{
class DBContext;
}

namespace idhan::filesystem
{
class ClusterAccess;
}

namespace idhan::metadata
{
struct MetadataError
{
	std::uint16_t status;
	std::string message;
};

// The caller keeps cluster alive until completion. Parsing runs before acquiring a transaction;
// metadata and file times commit together after the cluster identity is checked again.
[[nodiscard]] runtime::Task< std::expected< void, MetadataError > > parseAndStoreRecordMetadata(
	db::DBContext& db,
	RecordID record_id,
	MimeID mime_id,
	const filesystem::ClusterAccess& cluster,
	std::filesystem::path path );
} // namespace idhan::metadata
