#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>

#include "IDHANTypes.hpp"
#include "MetadataInfo.hpp"
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

[[nodiscard]] runtime::Task< std::expected< MetadataInfo, MetadataError > > parseRecordMetadata(
	RecordID record_id,
	MimeID mime_id,
	std::filesystem::path path );

// The caller keeps cluster and metadata alive until completion. Metadata and file times
// commit together, with cluster identity checks before the transaction and before commit.
[[nodiscard]] runtime::Task< std::expected< void, MetadataError > > storeRecordMetadata(
	db::DBContext& db,
	RecordID record_id,
	const filesystem::ClusterAccess& cluster,
	std::filesystem::path path,
	const MetadataInfo& metadata );

// The caller keeps cluster alive until completion. Parsing runs before acquiring a transaction.
[[nodiscard]] runtime::Task< std::expected< void, MetadataError > > parseAndStoreRecordMetadata(
	db::DBContext& db,
	RecordID record_id,
	MimeID mime_id,
	const filesystem::ClusterAccess& cluster,
	std::filesystem::path path );
} // namespace idhan::metadata
