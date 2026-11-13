#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>

namespace idhan::downloader
{
class SessionContext;

//! Shared by the queued operation and its optional JavaScript handle. Contains no
//! JavaScript values; the owning session's mutex guards completion and pending_id.
struct ImportStatus
{
	std::weak_ptr< SessionContext > session;
	std::uint64_t pending_id {};
	std::optional< std::expected< bool, std::string > > result_e;
};
} // namespace idhan::downloader
