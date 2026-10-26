#include "SessionDiagnostics.hpp"

#include <utility>

namespace idhan::downloader
{

std::string_view toString( const WorkPhase phase )
{
	switch ( phase )
	{
		case WorkPhase::QUEUED:
			return "queued";
		case WorkPhase::MODULE:
			return "module";
		case WorkPhase::PARSER:
			return "parser";
		case WorkPhase::TRANSFERRING:
			return "transferring";
	}

	return "unknown";
}

std::string_view toString( const SessionEventKind kind )
{
	switch ( kind )
	{
		case SessionEventKind::STARTED:
			return "started";
		case SessionEventKind::COMPLETED:
			return "completed";
		case SessionEventKind::FAILED:
			return "failed";
		case SessionEventKind::REQUEST:
			return "request";
		case SessionEventKind::REQUEST_FAILED:
			return "request_failed";
		case SessionEventKind::IMPORTED:
			return "imported";
		case SessionEventKind::IMPORT_FAILED:
			return "import_failed";
		case SessionEventKind::FOLLOWED:
			return "followed";
		case SessionEventKind::FINISHED:
			return "finished";
	}

	return "unknown";
}

std::string_view toString( const FollowStatus status )
{
	switch ( status )
	{
		case FollowStatus::QUEUED:
			return "queued";
		case FollowStatus::FILTERED:
			return "filtered";
		case FollowStatus::ALREADY_QUEUED:
			return "already_queued";
		case FollowStatus::ALREADY_EXPLORED:
			return "already_explored";
		case FollowStatus::ALREADY_IMPORTED:
			return "already_imported";
	}

	return "unknown";
}

void SessionDiagnostics::append( SessionEvent event )
{
	event.sequence = m_next_sequence++;
	event.at = std::chrono::system_clock::now();
	m_events.emplace_back( std::move( event ) );

	while ( m_events.size() > ring_capacity ) m_events.pop_front();
}

void SessionDiagnostics::recordStarted( const WorkInfo& info )
{
	const std::scoped_lock lock { m_mutex };
	++m_counters.work_started;
	append( SessionEvent { .kind = SessionEventKind::STARTED, .work = info.id, .url = info.url } );
}

void SessionDiagnostics::recordCompleted( const WorkInfo& info )
{
	const std::scoped_lock lock { m_mutex };
	++m_counters.work_completed;
	append( SessionEvent { .kind = SessionEventKind::COMPLETED, .work = info.id, .url = info.url } );
}

void SessionDiagnostics::recordFailed( const WorkInfo& info, const std::string& error )
{
	const std::scoped_lock lock { m_mutex };
	++m_counters.work_failed;
	append( SessionEvent { .kind = SessionEventKind::FAILED, .work = info.id, .url = info.url, .detail = error } );
}

void SessionDiagnostics::recordRequest( const RequestInfo& info )
{
	const std::scoped_lock lock { m_mutex };
	++m_counters.requests;
	m_counters.request_bytes += info.bytes;
	append(
		SessionEvent {
			.kind = SessionEventKind::REQUEST,
			.work = info.work,
			.url = info.url,
			.detail = info.lane,
			.status = info.status,
			.bytes = info.bytes } );
}

void SessionDiagnostics::recordRequestFailed( const RequestFailure& info )
{
	const std::scoped_lock lock { m_mutex };
	++m_counters.requests_failed;
	append(
		SessionEvent {
			.kind = SessionEventKind::REQUEST_FAILED,
			.work = info.work,
			.url = info.url,
			.detail = info.message,
			.status = info.status } );
}

void SessionDiagnostics::recordImported( const ImportInfo& info )
{
	const std::scoped_lock lock { m_mutex };
	++m_counters.imported;
	m_counters.import_bytes += info.size;
	append(
		SessionEvent {
			.kind = SessionEventKind::IMPORTED,
			.work = info.work,
			.url = info.url,
			.detail = info.content_type,
			.bytes = info.size,
			.record_id = info.record_id } );
}

void SessionDiagnostics::recordImportFailed( const WorkID work, const std::string& url, const std::string& error )
{
	const std::scoped_lock lock { m_mutex };
	++m_counters.import_failed;
	append( SessionEvent { .kind = SessionEventKind::IMPORT_FAILED, .work = work, .url = url, .detail = error } );
}

void SessionDiagnostics::recordFollowed( const WorkInfo& info, const FollowStatus status )
{
	const std::scoped_lock lock { m_mutex };

	switch ( status )
	{
		case FollowStatus::QUEUED:
			++m_counters.follows_queued;
			break;
		case FollowStatus::FILTERED:
			++m_counters.follows_filtered;
			break;
		case FollowStatus::ALREADY_QUEUED:
			++m_counters.follows_already_queued;
			break;
		case FollowStatus::ALREADY_EXPLORED:
			++m_counters.follows_already_explored;
			break;
		case FollowStatus::ALREADY_IMPORTED:
			++m_counters.follows_already_imported;
			break;
	}

	append(
		SessionEvent {
			.kind = SessionEventKind::FOLLOWED,
			.work = info.parent.value_or( 0 ),
			.url = info.url,
			.detail = std::string { toString( status ) } } );
}

void SessionDiagnostics::recordFinished()
{
	const std::scoped_lock lock { m_mutex };
	append( SessionEvent { .kind = SessionEventKind::FINISHED } );
}

void SessionDiagnostics::publish( LoopState state )
{
	const std::scoped_lock lock { m_mutex };
	m_loop = std::move( state );
}

SessionSnapshot SessionDiagnostics::snapshot( const std::uint64_t since ) const
{
	const std::scoped_lock lock { m_mutex };

	SessionSnapshot snapshot {};
	snapshot.counters = m_counters;
	snapshot.queued = m_loop.queued;
	snapshot.running = m_loop.work.size();
	snapshot.in_flight_requests = m_loop.in_flight_requests;
	snapshot.work = m_loop.work;
	snapshot.requests = m_loop.requests;
	snapshot.event_sequence = m_next_sequence - 1;

	for ( const SessionEvent& event : m_events )
	{
		if ( event.sequence > since ) snapshot.events.emplace_back( event );
	}

	if ( since != 0 && !m_events.empty() && m_events.front().sequence > since + 1 )
		snapshot.events_dropped = m_events.front().sequence - since - 1;

	return snapshot;
}

} // namespace idhan::downloader
