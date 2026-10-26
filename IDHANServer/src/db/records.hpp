#pragma once

#include <expected>
#include <optional>
#include <string>

#include "IDHANTypes.hpp"
#include "idhan/runtime/Task.hpp"

namespace idhan
{
class SHA256;
}

namespace idhan::db
{
class DBContext;
class DBTransaction;

[[nodiscard]] runtime::Task< std::optional< RecordID > > recordForHash( const SHA256& hash, DBContext& db );

[[nodiscard]] runtime::Task< std::expected< RecordID, std::string > > ensureRecord( const SHA256& hash, DBContext& db );

[[nodiscard]] runtime::Task< std::expected< RecordID, std::string > > ensureRecord(
	const SHA256& hash,
	DBTransaction& transaction );
} // namespace idhan::db
