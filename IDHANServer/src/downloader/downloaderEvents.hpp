#pragma once

#include <IDHANDownloader/session/SessionSnapshot.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "IDHANTypes.hpp"
#include "idhan/http/streaming/SSEEventHub.hpp"

//! Builders and publishers for the `downloader` SSE stream. Every payload carries a `type` field;
//! fields that do not apply are omitted rather than sent as null.
namespace idhan::downloader::events
{

struct UrlState
{
	std::string state {};
	std::optional< RecordID > record_id {};
	//! Byte size of the record's file, for imported and imported_previously.
	std::optional< std::int64_t > size {};
	//! Where a failed URL gave up: route, download, parser, import or closed.
	std::optional< std::string > stage {};
	std::optional< std::string > error {};
	std::optional< std::int64_t > finished_at {};
};

struct UrlRow
{
	DownloadSessionUrlID url_id {};
	std::optional< DownloadSessionUrlID > parent_id {};
	std::string url {};
	std::int64_t created_at {};
	UrlState state {};
	//! Live transfer progress, only for URLs that are downloading.
	std::optional< std::uint64_t > bytes {};
	std::optional< std::uint64_t > total {};
};

struct RequestError
{
	std::int64_t error_id {};
	std::optional< DownloadSessionUrlID > url_id {};
	std::string url {};
	std::string lane {};
	std::optional< std::int32_t > http_status {};
	std::optional< std::string > message {};
	std::int64_t occurred_at {};
};

struct SessionInfo
{
	DownloadSessionID id {};
	std::string name {};
	std::int64_t created_at {};
	std::int64_t last_used_at {};
	bool is_subscription {};
};

struct SessionBandwidth
{
	DownloadSessionID session_id {};
	std::uint64_t download_bytes_per_second {};
};

struct Bandwidth
{
	std::uint64_t download_bytes_per_second {};
	std::vector< SessionBandwidth > sessions;
};

void bandwidth( const Bandwidth& speeds );
[[nodiscard]] http::SSEInitialEvent bandwidthSnapshot( const Bandwidth& speeds );

//! URL rows per urls_snapshot event, and error rows per errors_snapshot event.
inline constexpr std::size_t snapshot_chunk_size { 1000 };

void setSubscriptionSession( DownloadSessionID session_id, bool subscription );

[[nodiscard]] bool interested( DownloadSessionID session_id );

void urlCreated( DownloadSessionID session_id, const UrlRow& row );
void urlUpdated( DownloadSessionID session_id, DownloadSessionUrlID url_id, const UrlState& state );
void urlRemoved( DownloadSessionID session_id, DownloadSessionUrlID url_id );
//! total is 0 when the size is unknown.
void urlProgress( DownloadSessionID session_id, DownloadSessionUrlID url_id, std::uint64_t bytes, std::uint64_t total );
void requestError( DownloadSessionID session_id, const RequestError& error );
void requestErrorsCleared( DownloadSessionID session_id, const std::vector< std::int64_t >& error_ids );
void sessionCreated( const SessionInfo& session );
void sessionState( DownloadSessionID session_id, std::string_view state, std::optional< std::int64_t > last_used_at );
void sessionDeleted( DownloadSessionID session_id );
void network( DownloadSessionID session_id, const SessionSnapshot& snapshot );

[[nodiscard]] http::SSEInitialEvent sessionSnapshot( const SessionInfo& session, std::string_view state );
[[nodiscard]] http::SSEInitialEvent urlsSnapshot( DownloadSessionID session_id, std::span< const UrlRow > rows );
[[nodiscard]] http::SSEInitialEvent errorsSnapshot(
	DownloadSessionID session_id,
	std::span< const RequestError > errors );
[[nodiscard]] http::SSEInitialEvent networkSnapshot( DownloadSessionID session_id, const SessionSnapshot& snapshot );
[[nodiscard]] http::SSEInitialEvent snapshotComplete();

} // namespace idhan::downloader::events
