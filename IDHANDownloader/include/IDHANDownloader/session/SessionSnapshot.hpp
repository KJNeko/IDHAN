#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "../../../../IDHAN/include/IDHANTypes.hpp"
#include "IDHANDownloader/http/RequestTiming.hpp"
#include "IDHANDownloader/session/SessionObserver.hpp"

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

enum class ParserActivity : std::uint8_t
{
	READY,
	WAITING_REQUEST,
	WAITING_IMPORT,
	WAITING_HOST,
	WAITING_PROMISE,
	FINISHING,
};

struct ParserActivityCounts
{
	std::size_t ready {};
	std::size_t waiting_requests {};
	std::size_t waiting_imports {};
	std::size_t waiting_host {};
	std::size_t waiting_promises {};
	std::size_t finishing {};
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
	ParserActivity activity {};
	std::size_t pending_requests {};
	std::size_t pending_imports {};
	std::size_t pending_host {};
	std::chrono::steady_clock::time_point started_at {};
};

struct PendingRequestSnapshot
{
	WorkID work {};
	std::string url {};
	std::string lane {};
	bool import {};
	bool host {};
	bool ready {};
	//! Body bytes of the current hop received so far.
	std::uint64_t bytes {};
	//! Content-Length of the current hop; 0 when unknown.
	std::uint64_t total {};
	RequestTiming timing {};
};

struct SessionCounters
{
	std::uint64_t work_started {};
	std::uint64_t work_completed {};
	std::uint64_t work_failed {};
	std::uint64_t requests {};
	std::uint64_t request_bytes {};
	std::uint64_t requests_failed {};
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
	REQUEST_FAILED,
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
	//! Unset for every kind that carries no response.
	std::optional< std::int32_t > status {};
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
	std::size_t downloading {};
	std::size_t waiting {};
	std::size_t waiting_parsers {};
	std::size_t waiting_requests {};
	std::size_t waiting_http_rate {};
	std::size_t waiting_http_shard {};
	std::size_t waiting_storage {};
	std::uint64_t downloaded_bytes {};
	std::uint64_t average_import_bytes {};
	std::uint64_t download_bytes_per_second {};
	std::size_t in_flight_requests {};
	std::size_t in_flight_limit {};
	std::size_t outstanding {};
	ParserActivityCounts parsers {};
	//! Most recently used parser instance/thread. A session may use several parsers.
	std::size_t script_worker {};
	bool script_executing {};
	// Activity of parser instances used by this session, shared with other sessions.
	// Occupancy includes synchronous host callbacks; awaiting operations is excluded.
	std::uint64_t script_busy_ns {};
	std::uint64_t script_bursts {};
	std::size_t imports_queued {};
	std::size_t imports_active {};
	std::size_t host_queued {};
	std::size_t host_active {};
	SessionCounters counters {};
	std::vector< WorkSnapshot > work {};
	std::vector< PendingRequestSnapshot > requests {};
	//! Events after the requested cursor, oldest first.
	std::vector< SessionEvent > events {};
	std::uint64_t event_sequence {};
	std::uint64_t events_dropped {};
};

[[nodiscard]] std::string_view toString( WorkPhase phase );
[[nodiscard]] std::string_view toString( ParserActivity activity );
[[nodiscard]] std::string_view toString( SessionEventKind kind );
[[nodiscard]] std::string_view toString( FollowStatus status );

} // namespace idhan::downloader
