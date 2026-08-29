#include "DownloaderDebugAPI.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "api/helpers/createBadRequest.hpp"
#include "db/drogonArrayBind.hpp"
#include "downloader/DownloadSessionManager.hpp"

namespace idhan::api
{
using namespace idhan::downloader;

using SteadyClock = std::chrono::steady_clock;

static Json::UInt64 ageMs( const SteadyClock::time_point started_at, const SteadyClock::time_point now )
{
	if ( started_at.time_since_epoch().count() == 0 ) return 0;

	const auto elapsed { std::max( SteadyClock::duration::zero(), now - started_at ) };
	return static_cast< Json::UInt64 >( std::chrono::round< std::chrono::milliseconds >( elapsed ).count() );
}

static Json::Value rowOrNull( const std::unordered_map< WorkID, DownloadSessionUrlID >& rows, const WorkID work )
{
	const auto found { rows.find( work ) };

	if ( found == rows.end() || found->second == 0 ) return Json::Value {};

	return Json::Value { found->second };
}

static std::string_view stateOf( const SessionSnapshot& snapshot )
{
	if ( snapshot.cancelled ) return "cancelled";
	if ( snapshot.closed ) return "closing";
	if ( snapshot.outstanding > 0 || snapshot.in_flight_requests > 0 ) return "running";

	return "idle";
}

static Json::Value countersJson( const SessionCounters& counters )
{
	Json::Value json {};
	json[ "work_started" ] = Json::UInt64 { counters.work_started };
	json[ "work_completed" ] = Json::UInt64 { counters.work_completed };
	json[ "work_failed" ] = Json::UInt64 { counters.work_failed };
	json[ "requests" ] = Json::UInt64 { counters.requests };
	json[ "request_bytes" ] = Json::UInt64 { counters.request_bytes };
	json[ "imported" ] = Json::UInt64 { counters.imported };
	json[ "import_bytes" ] = Json::UInt64 { counters.import_bytes };
	json[ "import_failed" ] = Json::UInt64 { counters.import_failed };
	json[ "follows_queued" ] = Json::UInt64 { counters.follows_queued };
	json[ "follows_filtered" ] = Json::UInt64 { counters.follows_filtered };
	json[ "follows_already_queued" ] = Json::UInt64 { counters.follows_already_queued };
	json[ "follows_already_explored" ] = Json::UInt64 { counters.follows_already_explored };
	json[ "follows_already_imported" ] = Json::UInt64 { counters.follows_already_imported };
	return json;
}

static Json::Value workJson(
	const WorkSnapshot& work,
	const std::unordered_map< WorkID, DownloadSessionUrlID >& rows,
	const SteadyClock::time_point now )
{
	Json::Value json {};
	json[ "work_id" ] = Json::UInt64 { work.id };
	json[ "parent_work_id" ] = work.parent.has_value() ? Json::Value { Json::UInt64 { *work.parent } } : Json::Value {};
	json[ "url_id" ] = rowOrNull( rows, work.id );
	json[ "url" ] = work.url;
	json[ "url_class" ] = work.url_class;
	json[ "parser" ] = work.parser;
	json[ "phase" ] = std::string { toString( work.phase ) };
	json[ "outstanding" ] = Json::UInt64 { work.outstanding };
	json[ "age_ms" ] = ageMs( work.started_at, now );
	return json;
}

static Json::Value requestJson(
	const PendingRequestSnapshot& request,
	const std::unordered_map< WorkID, DownloadSessionUrlID >& rows,
	const SteadyClock::time_point now )
{
	Json::Value json {};
	json[ "work_id" ] = Json::UInt64 { request.work };
	json[ "url_id" ] = rowOrNull( rows, request.work );
	json[ "url" ] = request.url;
	json[ "lane" ] = request.lane;
	json[ "import" ] = request.import;
	json[ "age_ms" ] = ageMs( request.started_at, now );
	return json;
}

static Json::Value eventJson(
	const SessionEvent& event,
	const std::unordered_map< WorkID, DownloadSessionUrlID >& rows )
{
	Json::Value json {};
	json[ "sequence" ] = Json::UInt64 { event.sequence };
	json[ "at" ] = Json::Int64 {
		std::chrono::duration_cast< std::chrono::microseconds >( event.at.time_since_epoch() ).count()
	};
	json[ "kind" ] = std::string { toString( event.kind ) };
	json[ "work_id" ] = Json::UInt64 { event.work };
	json[ "url_id" ] = rowOrNull( rows, event.work );
	json[ "url" ] = event.url;
	json[ "detail" ] = event.detail;
	json[ "status" ] = event.status;
	json[ "bytes" ] = Json::UInt64 { event.bytes };
	json[ "record_id" ] = event.record_id == 0 ? Json::Value {} : Json::Value { event.record_id };
	return json;
}

static Json::Value laneJson( const LaneSnapshot& lane )
{
	Json::Value json {};
	json[ "scheduling_key" ] = lane.key;
	json[ "group" ] = lane.group.value_or( "" );
	json[ "throttled" ] = lane.throttled;
	json[ "effective_interval_ms" ] = static_cast< Json::UInt64 >(
		std::chrono::ceil< std::chrono::milliseconds >( lane.effective_interval ).count() );
	json[ "remaining_ms" ] =
		static_cast< Json::UInt64 >( std::chrono::ceil< std::chrono::milliseconds >( lane.remaining ).count() );
	json[ "consecutive_limits" ] = lane.consecutive_failures;
	json[ "backed_off" ] = lane.backed_off;
	json[ "in_flight" ] = Json::UInt64 { lane.in_flight };
	json[ "queued" ] = Json::UInt64 { lane.queued };
	json[ "shards" ] = Json::UInt64 { lane.shards };
	json[ "bytes_per_second" ] = Json::UInt64 { lane.bytes_per_second };
	json[ "active" ] = lane.active;
	return json;
}

static bool parseCursors( const std::string& text, std::unordered_map< DownloadSessionID, std::uint64_t >& cursors )
{
	std::string_view remaining { text };

	while ( !remaining.empty() )
	{
		const auto comma { remaining.find( ',' ) };
		const std::string_view pair { remaining.substr( 0, comma ) };
		remaining = comma == std::string_view::npos ? std::string_view {} : remaining.substr( comma + 1 );

		const auto colon { pair.find( ':' ) };

		if ( colon == std::string_view::npos ) return false;

		const std::string_view id_text { pair.substr( 0, colon ) };
		const std::string_view sequence_text { pair.substr( colon + 1 ) };

		DownloadSessionID id { 0 };
		std::uint64_t sequence { 0 };

		const auto id_result { std::from_chars( id_text.data(), id_text.data() + id_text.size(), id ) };
		const auto sequence_result {
			std::from_chars( sequence_text.data(), sequence_text.data() + sequence_text.size(), sequence )
		};

		if ( id_result.ec != std::errc {} || id_result.ptr != id_text.data() + id_text.size() ) return false;
		if ( sequence_result.ec != std::errc {} || sequence_result.ptr != sequence_text.data() + sequence_text.size() )
			return false;

		cursors.insert_or_assign( id, sequence );
	}

	return true;
}

drogon::Task< drogon::HttpResponsePtr > DownloaderDebugAPI::debug( drogon::HttpRequestPtr request )
{
	std::unordered_map< DownloadSessionID, std::uint64_t > cursors {};

	if ( !parseCursors( request->getParameter( "since" ), cursors ) )
		co_return createBadRequest( "The 'since' cursor must be a comma separated list of <session_id>:<sequence>" );

	auto infos { downloadSessionManager().debugSnapshots( cursors ) };

	std::vector< DownloadSessionID > ids {};
	ids.reserve( infos.size() );
	for ( const auto& info : infos ) ids.emplace_back( info.id );

	std::unordered_map< DownloadSessionID, std::string > names {};

	if ( !ids.empty() )
	{
		const auto db { drogon::app().getDbClient() };
		const auto rows { co_await db->execSqlCoro(
			"SELECT download_session_id, name FROM download_sessions WHERE download_session_id = ANY($1::BIGINT[])",
			std::move( ids ) ) };

		for ( const auto& row : rows )
			names.emplace( row[ "download_session_id" ].as< DownloadSessionID >(), row[ "name" ].as< std::string >() );
	}

	const auto now { SteadyClock::now() };
	Json::Value sessions { Json::arrayValue };

	for ( const auto& info : infos )
	{
		const SessionSnapshot& snapshot { info.snapshot };
		Json::Value json {};
		json[ "id" ] = info.id;

		if ( const auto name { names.find( info.id ) }; name != names.end() )
			json[ "name" ] = name->second;
		else
			json[ "name" ] = Json::Value {};

		json[ "root_url" ] = snapshot.root_url;
		json[ "state" ] = std::string { stateOf( snapshot ) };
		json[ "closed" ] = snapshot.closed;
		json[ "cancelled" ] = snapshot.cancelled;
		json[ "idle" ] = snapshot.idle;
		json[ "queued" ] = Json::UInt64 { snapshot.queued };
		json[ "running" ] = Json::UInt64 { snapshot.running };
		json[ "in_flight" ] = Json::UInt64 { snapshot.in_flight_requests };
		json[ "in_flight_limit" ] = Json::UInt64 { snapshot.in_flight_limit };
		json[ "outstanding" ] = Json::UInt64 { snapshot.outstanding };
		json[ "counters" ] = countersJson( snapshot.counters );

		Json::Value work { Json::arrayValue };
		for ( const auto& item : snapshot.work ) work.append( workJson( item, info.rows, now ) );
		json[ "work" ] = work;

		Json::Value requests { Json::arrayValue };
		for ( const auto& pending : snapshot.requests ) requests.append( requestJson( pending, info.rows, now ) );
		json[ "requests" ] = requests;

		Json::Value events { Json::arrayValue };
		for ( const auto& event : snapshot.events ) events.append( eventJson( event, info.rows ) );
		json[ "events" ] = events;

		json[ "event_sequence" ] = Json::UInt64 { snapshot.event_sequence };
		json[ "events_dropped" ] = Json::UInt64 { snapshot.events_dropped };
		sessions.append( json );
	}

	auto lanes_snapshot { downloadSessionManager().laneSnapshots() };
	std::ranges::sort( lanes_snapshot, {}, &LaneSnapshot::key );
	Json::Value lanes { Json::arrayValue };
	for ( const auto& lane : lanes_snapshot ) lanes.append( laneJson( lane ) );

	Json::Value output {};
	output[ "sessions" ] = sessions;
	output[ "lanes" ] = lanes;
	co_return drogon::HttpResponse::newHttpJsonResponse( output );
}

} // namespace idhan::api
