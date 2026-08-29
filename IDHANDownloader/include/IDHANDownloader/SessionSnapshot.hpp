#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "../../../IDHAN/include/IDHANTypes.hpp"
#include "SessionObserver.hpp"

namespace idhan::downloader
{

enum class WorkPhase : std::uint8_t
{
	QUEUED,
	MODULE,
	PARSER,
	//! The script returned while an unawaited request still retains its realm.
	TRANSFERRING,
};

struct WorkSnapshot
{
	WorkID id {};
	std::optional< WorkID > parent {};
	std::string url {};
	std::string url_class {};
	std::string parser {};
	WorkPhase phase {};
	std::size_t outstanding {};
	std::chrono::steady_clock::time_point started_at {};
};

struct PendingRequestSnapshot
{
	WorkID work {};
	std::string url {};
	std::string lane {};
	bool import {};
	std::chrono::steady_clock::time_point started_at {};
};

struct SessionCounters
{
	std::uint64_t work_started {};
	std::uint64_t work_completed {};
	std::uint64_t work_failed {};
	std::uint64_t requests {};
	std::uint64_t request_bytes {};
	std::uint64_t imported {};
	std::uint64_t import_bytes {};
	std::uint64_t import_failed {};
	std::uint64_t follows_queued {};
	std::uint64_t follows_filtered {};
	std::uint64_t follows_already_queued {};
	std::uint64_t follows_already_explored {};
	std::uint64_t follows_already_imported {};
};

enum class SessionEventKind : std::uint8_t
{
	STARTED,
	COMPLETED,
	FAILED,
	REQUEST,
	IMPORTED,
	IMPORT_FAILED,
	FOLLOWED,
	FINISHED,
};

struct SessionEvent
{
	std::uint64_t sequence {};
	std::chrono::system_clock::time_point at {};
	SessionEventKind kind {};
	WorkID work {};
	std::string url {};
	//! Meaning depends on kind.
	std::string detail {};
	std::int32_t status {};
	std::size_t bytes {};
	RecordID record_id {};
};

struct SessionSnapshot
{
	std::string root_url {};
	bool closed {};
	bool cancelled {};
	bool idle {};
	std::size_t queued {};
	std::size_t running {};
	std::size_t in_flight_requests {};
	std::size_t in_flight_limit {};
	std::size_t outstanding {};
	SessionCounters counters {};
	std::vector< WorkSnapshot > work {};
	std::vector< PendingRequestSnapshot > requests {};
	//! Events after the requested cursor, oldest first.
	std::vector< SessionEvent > events {};
	std::uint64_t event_sequence {};
	std::uint64_t events_dropped {};
};

[[nodiscard]] std::string_view toString( WorkPhase phase );
[[nodiscard]] std::string_view toString( SessionEventKind kind );
[[nodiscard]] std::string_view toString( FollowStatus status );

} // namespace idhan::downloader
