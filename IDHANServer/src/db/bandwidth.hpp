#pragma once

#include <IDHANDownloader/http/Bandwidth.hpp>

#include <string>
#include <vector>

#include "idhan/runtime/Task.hpp"

namespace idhan::db
{
class DBContext;

struct HostBandwidth
{
	std::string host;
	std::int64_t bytes_received {};
};

[[nodiscard]] runtime::Task<> storeBandwidth( DBContext& db, const std::vector< downloader::BandwidthLog >& logs );
[[nodiscard]] runtime::Task< std::vector< HostBandwidth > > bandwidthTotals(
	DBContext& db,
	std::int64_t start,
	std::int64_t end );
} // namespace idhan::db
