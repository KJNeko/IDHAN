#include "DownloadSessionEventHub.hpp"

#include <algorithm>
#include <span>

#include "DownloadSessionManager.hpp"
#include "downloaderEvents.hpp"
#include "idhan/db/DBContext.hpp"

namespace idhan::downloader
{
//! Smaller transfers finish too quickly for progress to be worth sending.
constexpr std::uint64_t progress_threshold_bytes { 1024 * 1024 };

static bool reportsProgress( const UrlTransfer& transfer )
{
	if ( transfer.total > 0 ) return transfer.total > progress_threshold_bytes;

	return transfer.bytes >= progress_threshold_bytes;
}

DownloadSessionEventHub& DownloadSessionEventHub::instance()
{
	static DownloadSessionEventHub hub;
	return hub;
}

void DownloadSessionEventHub::started( const DownloadSessionID id, const std::optional< std::int64_t > last_used_at )
{
	// Publishing under the lock keeps active and idle in the order they were decided.
	const std::lock_guard lock { m_mutex };
	++m_generations[ id ];
	const bool newly_active { m_active.insert( id ).second };
	if ( !newly_active && !last_used_at.has_value() ) return;

	events::sessionState( id, "active", last_used_at );
}

void DownloadSessionEventHub::finished( const DownloadSessionID id )
{
	std::uint64_t generation {};
	{
		const std::lock_guard lock { m_mutex };
		if ( !m_active.contains( id ) ) return;

		generation = m_generations[ id ];
	}

	if ( !downloadSessionManager().idle( id ) ) return;

	const auto snapshot { downloadSessionManager().networkSnapshot( id ) };

	const std::lock_guard lock { m_mutex };
	if ( m_generations[ id ] != generation ) return;

	if ( !m_active.erase( id ) ) return;

	m_progress.erase( id );
	m_network.erase( id );
	events::network( id, snapshot );
	events::sessionState( id, "idle", std::nullopt );
}

void DownloadSessionEventHub::removed( const DownloadSessionID id )
{
	const std::lock_guard lock { m_mutex };
	m_active.erase( id );
	m_generations.erase( id );
	m_progress.erase( id );
	m_network.erase( id );
	events::sessionDeleted( id );
}

void DownloadSessionEventHub::flush()
{
	publishBandwidth();
	std::vector< DownloadSessionID > active;
	{
		const std::lock_guard lock { m_mutex };
		active.assign( m_active.begin(), m_active.end() );
	}

	for ( const auto id : active )
	{
		const auto snapshot { downloadSessionManager().networkSnapshot( id ) };
		const auto transfers { downloadSessionManager().transfers( id, snapshot ) };
		const Network network {
			.downloading = snapshot.downloading,
			.waiting = snapshot.waiting,
			.waiting_parsers = snapshot.waiting_parsers,
			.waiting_requests = snapshot.waiting_requests,
			.waiting_http_rate = snapshot.waiting_http_rate,
			.waiting_http_shard = snapshot.waiting_http_shard,
			.waiting_storage = snapshot.waiting_storage,
			.downloaded_bytes = snapshot.downloaded_bytes,
			.average_import_bytes = snapshot.average_import_bytes
		};
		{
			const std::lock_guard lock { m_mutex };
			if ( !m_active.contains( id ) ) continue;

			const auto previous { m_network.find( id ) };
			const bool network_changed { previous == m_network.end() || previous->second != network };
			if ( network_changed ) events::network( id, snapshot );
			m_network.insert_or_assign( id, network );

			auto& published { m_progress[ id ] };
			std::unordered_map< DownloadSessionUrlID, Progress > current;
			for ( const auto& transfer : transfers )
			{
				if ( !reportsProgress( transfer ) ) continue;

				const Progress progress { .bytes = transfer.bytes, .total = transfer.total };
				current.emplace( transfer.url_id, progress );
				const auto sent { published.find( transfer.url_id ) };
				if ( sent != published.end() && sent->second == progress ) continue;

				events::urlProgress( id, transfer.url_id, transfer.bytes, transfer.total );
			}
			published = std::move( current );
		}

		// Also catches a completion notification that arrived while the session still looked busy.
		if ( downloadSessionManager().idle( id ) ) finished( id );
	}
}

void DownloadSessionEventHub::publishBandwidth()
{
	const auto totals { downloadSessionManager().downloadedBytes() };
	const auto now { std::chrono::steady_clock::now() };
	const std::lock_guard lock { m_mutex };
	if ( now - m_speed_at < std::chrono::seconds { 1 } ) return;

	events::Bandwidth speeds;
	std::unordered_set< DownloadSessionID > present;
	for ( const auto& [ id, total ] : totals )
	{
		present.insert( id );
		auto [ entry, inserted ] { m_speed_samples.try_emplace( id, m_speed_at ) };
		const auto speed { entry->second.sample( total, now ) };
		speeds.download_bytes_per_second += speed;
		speeds.sessions.push_back( { id, speed } );
	}
	std::erase_if( m_speed_samples, [ &present ]( const auto& entry ) { return !present.contains( entry.first ); } );
	std::ranges::sort( speeds.sessions, {}, &events::SessionBandwidth::session_id );
	m_speed_at = now;
	const bool already_zero { m_bandwidth.download_bytes_per_second == 0 };
	m_bandwidth = std::move( speeds );
	if ( already_zero && m_bandwidth.download_bytes_per_second == 0 ) return;

	events::bandwidth( m_bandwidth );
}

static std::string_view liveState(
	const std::string& stored,
	const std::unordered_map< DownloadSessionUrlID, std::string >& live,
	const DownloadSessionUrlID url_id )
{
	if ( stored != "pending" && stored != "processing" ) return stored;

	const auto found { live.find( url_id ) };
	if ( found == live.end() ) return stored;

	return found->second;
}

template < typename T >
static void appendChunks(
	std::vector< http::SSEInitialEvent >& output,
	const DownloadSessionID session_id,
	const std::vector< T >& items,
	http::SSEInitialEvent ( *build )( DownloadSessionID, std::span< const T > ) )
{
	for ( std::size_t offset {}; offset < items.size(); offset += events::snapshot_chunk_size )
	{
		const auto count { std::min( events::snapshot_chunk_size, items.size() - offset ) };
		output.push_back( build( session_id, std::span< const T > { items }.subspan( offset, count ) ) );
	}
}

runtime::Task< void > DownloadSessionEventHub::initializeSubscription(
	db::DBContext& db,
	http::SSESubscription& subscription,
	std::vector< DownloadSessionID > ids )
{
	// Changes published after this point stay queued behind the baseline read below.
	const auto checkpoint { subscription.checkpoint() };

	db::DBBindings session_bindings;
	session_bindings.bind( ids );
	const auto sessions {
		co_await db
			.query(
				"SELECT sessions.download_session_id, sessions.name, "
				"extract(epoch FROM sessions.created_at)::bigint, extract(epoch FROM sessions.last_used_at)::bigint, "
				"EXISTS (SELECT 1 FROM download_session_urls urls "
				"WHERE urls.download_session_id = sessions.download_session_id "
				"AND urls.state IN ('pending', 'processing')) "
				"FROM download_sessions sessions "
				"WHERE (cardinality($1::bigint[]) = 0 AND NOT sessions.is_subscription) OR sessions.download_session_id = ANY($1::bigint[]) "
				"ORDER BY sessions.download_session_id",
				std::move( session_bindings ) )
			.rows< DownloadSessionID, std::string, std::int64_t, std::int64_t, bool >()
	};

	db::DBBindings url_bindings;
	url_bindings.bind( ids );
	const auto urls {
		co_await db
			.query(
				"SELECT urls.download_session_id, urls.download_session_url_id, urls.parent_url_id, urls.url, "
				"urls.state, urls.record_id, file_info.size, failures.stage, failures.message, "
				"extract(epoch FROM urls.created_at)::bigint, extract(epoch FROM urls.finished_at)::bigint "
				"FROM download_session_urls urls "
				"LEFT JOIN file_info ON file_info.record_id = urls.record_id "
				"LEFT JOIN download_session_url_failures failures "
				"ON failures.download_session_id = urls.download_session_id "
				"AND failures.download_session_url_id = urls.download_session_url_id "
				"WHERE (cardinality($1::bigint[]) = 0 AND EXISTS (SELECT 1 FROM download_sessions sessions WHERE sessions.download_session_id=urls.download_session_id AND NOT sessions.is_subscription)) OR urls.download_session_id = ANY($1::bigint[]) "
				"ORDER BY urls.download_session_id, urls.download_session_url_id",
				std::move( url_bindings ) )
			.rows<
				DownloadSessionID,
				DownloadSessionUrlID,
				std::optional< DownloadSessionUrlID >,
				std::string,
				std::string,
				std::optional< RecordID >,
				std::optional< std::int64_t >,
				std::optional< std::string >,
				std::optional< std::string >,
				std::int64_t,
				std::optional< std::int64_t > >()
	};

	db::DBBindings error_bindings;
	error_bindings.bind( ids );
	const auto errors {
		co_await db
			.query(
				"SELECT download_session_id, download_session_error_id, download_session_url_id, url, lane, status, "
				"message, extract(epoch FROM occurred_at)::bigint "
				"FROM download_session_errors "
				"WHERE (cardinality($1::bigint[]) = 0 AND EXISTS (SELECT 1 FROM download_sessions sessions WHERE sessions.download_session_id=download_session_errors.download_session_id AND NOT sessions.is_subscription)) OR download_session_id = ANY($1::bigint[]) "
				"ORDER BY download_session_id, download_session_error_id",
				std::move( error_bindings ) )
			.rows<
				DownloadSessionID,
				std::int64_t,
				std::optional< DownloadSessionUrlID >,
				std::string,
				std::string,
				std::optional< std::int32_t >,
				std::optional< std::string >,
				std::int64_t >()
	};

	std::unordered_set< DownloadSessionID > active;
	{
		const std::lock_guard lock { m_mutex };
		active = m_active;
	}

	std::unordered_map< DownloadSessionID, std::vector< events::UrlRow > > session_urls;
	for (
		const auto& [ session_id, url_id, parent_id, url, state, record_id, size, stage, error, created_at, finished_at ] :
		urls )
	{
		session_urls[ session_id ].push_back(
			events::UrlRow {
				.url_id = url_id,
				.parent_id = parent_id,
				.url = url,
				.created_at = created_at,
				.state = events::UrlState {
					.state = state,
					.record_id = record_id,
					.size = size,
					.stage = stage,
					.error = error,
					.finished_at = finished_at } } );
	}

	std::unordered_map< DownloadSessionID, std::vector< events::RequestError > > session_errors;
	for ( const auto& [ session_id, error_id, url_id, url, lane, status, message, occurred_at ] : errors )
	{
		session_errors[ session_id ].push_back(
			events::RequestError {
				.error_id = error_id,
				.url_id = url_id,
				.url = url,
				.lane = lane,
				.http_status = status,
				.message = message,
				.occurred_at = occurred_at } );
	}

	std::vector< http::SSEInitialEvent > initial;
	std::vector< DownloadSessionID > running;
	for ( const auto& [ session_id, name, created_at, last_used_at, unfinished ] : sessions )
	{
		const bool is_active { active.contains( session_id ) || unfinished };
		if ( active.contains( session_id ) ) running.push_back( session_id );

		initial.push_back(
			events::sessionSnapshot(
				events::SessionInfo {
					.id = session_id, .name = name, .created_at = created_at, .last_used_at = last_used_at },
				is_active ? "active" : "idle" ) );
	}

	std::unordered_map< DownloadSessionID, SessionSnapshot > networks;
	for ( const auto session_id : running )
	{
		const auto [ entry, inserted ] {
			networks.emplace( session_id, downloadSessionManager().networkSnapshot( session_id ) )
		};
		initial.push_back( events::networkSnapshot( session_id, entry->second ) );
	}

	for ( const auto& [ session_id, name, created_at, last_used_at, unfinished ] : sessions )
	{
		auto& rows { session_urls[ session_id ] };
		if ( active.contains( session_id ) )
		{
			const auto live { downloadSessionManager().liveStates( session_id ) };
			std::unordered_map< DownloadSessionUrlID, UrlTransfer > transfers;
			for ( const auto& transfer : downloadSessionManager().transfers( session_id, networks[ session_id ] ) )
				transfers.emplace( transfer.url_id, transfer );

			for ( auto& row : rows )
			{
				row.state.state = std::string { liveState( row.state.state, live, row.url_id ) };
				if ( row.state.state != "downloading" ) continue;

				const auto transfer { transfers.find( row.url_id ) };
				if ( transfer == transfers.end() ) continue;

				row.bytes = transfer->second.bytes;
				if ( transfer->second.total > 0 ) row.total = transfer->second.total;
			}
		}

		appendChunks< events::UrlRow >( initial, session_id, rows, &events::urlsSnapshot );
		appendChunks< events::RequestError >(
			initial, session_id, session_errors[ session_id ], &events::errorsSnapshot );
	}

	{
		const std::lock_guard lock { m_mutex };
		initial.push_back( events::bandwidthSnapshot( m_bandwidth ) );
	}
	initial.push_back( events::snapshotComplete() );
	subscription.prependSnapshots( std::move( initial ), checkpoint );
}
} // namespace idhan::downloader
