#include "downloaderDebug.hpp"

#include <folly/coro/Sleep.h>
#include <folly/io/IOBuf.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <format>
#include <memory>
#include <simdjson.h>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "IDHANTypes.hpp"
#include "apiAuth.hpp"
#include "downloader/DebugEvents.hpp"
#include "downloader/DownloadSessionManager.hpp"
#include "idhan/db/DBContext.hpp"
#include "idhan/http/SSEBodySource.hpp"

namespace idhan::httpapi
{

using SteadyClock = std::chrono::steady_clock;

[[nodiscard]] static std::uint64_t ageMs( const SteadyClock::time_point started_at, const SteadyClock::time_point now )
{
	if ( started_at.time_since_epoch().count() == 0 ) return 0;

	const auto elapsed { std::max( SteadyClock::duration::zero(), now - started_at ) };
	return static_cast< std::uint64_t >( std::chrono::round< std::chrono::milliseconds >( elapsed ).count() );
}

[[nodiscard]] static std::optional< DownloadSessionUrlID > urlIdFor(
	const std::unordered_map< downloader::WorkID, DownloadSessionUrlID >& rows,
	const downloader::WorkID work )
{
	const auto found { rows.find( work ) };
	if ( found == rows.end() || found->second == 0 ) return std::nullopt;
	return found->second;
}

[[nodiscard]] static std::string_view stateOf( const downloader::SessionSnapshot& snapshot )
{
	if ( snapshot.cancelled ) return "cancelled";
	if ( snapshot.closed ) return "closing";
	if ( snapshot.outstanding > 0 || snapshot.in_flight_requests > 0 ) return "running";
	return "idle";
}

static void appendCounters( simdjson::builder::string_builder& builder, const downloader::SessionCounters& counters )
{
	builder.start_object();
	builder.append_key_value( "work_started", counters.work_started );
	builder.append_comma();
	builder.append_key_value( "work_completed", counters.work_completed );
	builder.append_comma();
	builder.append_key_value( "work_failed", counters.work_failed );
	builder.append_comma();
	builder.append_key_value( "requests", counters.requests );
	builder.append_comma();
	builder.append_key_value( "request_bytes", counters.request_bytes );
	builder.append_comma();
	builder.append_key_value( "requests_failed", counters.requests_failed );
	builder.append_comma();
	builder.append_key_value( "imported", counters.imported );
	builder.append_comma();
	builder.append_key_value( "import_bytes", counters.import_bytes );
	builder.append_comma();
	builder.append_key_value( "import_failed", counters.import_failed );
	builder.append_comma();
	builder.append_key_value( "follows_queued", counters.follows_queued );
	builder.append_comma();
	builder.append_key_value( "follows_filtered", counters.follows_filtered );
	builder.append_comma();
	builder.append_key_value( "follows_already_queued", counters.follows_already_queued );
	builder.append_comma();
	builder.append_key_value( "follows_already_explored", counters.follows_already_explored );
	builder.append_comma();
	builder.append_key_value( "follows_already_imported", counters.follows_already_imported );
	builder.end_object();
}

static void appendWork(
	simdjson::builder::string_builder& builder,
	const downloader::WorkSnapshot& work,
	const std::unordered_map< downloader::WorkID, DownloadSessionUrlID >& rows,
	const SteadyClock::time_point now )
{
	builder.start_object();
	builder.append_key_value( "work_id", work.id );
	builder.append_comma();
	builder.escape_and_append_with_quotes( "parent_work_id" );
	builder.append_colon();
	if ( work.parent )
		builder.append( *work.parent );
	else
		builder.append_raw( "null" );
	builder.append_comma();
	builder.escape_and_append_with_quotes( "url_id" );
	builder.append_colon();
	if ( const auto id { urlIdFor( rows, work.id ) } )
		builder.append( *id );
	else
		builder.append_raw( "null" );
	builder.append_comma();
	builder.append_key_value( "url", work.url );
	builder.append_comma();
	builder.append_key_value( "url_class", work.url_class );
	builder.append_comma();
	builder.append_key_value( "parser", work.parser );
	builder.append_comma();
	builder.append_key_value( "phase", downloader::toString( work.phase ) );
	builder.append_comma();
	builder.append_key_value( "outstanding", static_cast< std::uint64_t >( work.outstanding ) );
	builder.append_comma();
	builder.append_key_value( "age_ms", ageMs( work.started_at, now ) );
	builder.end_object();
}

static void appendRequest(
	simdjson::builder::string_builder& builder,
	const downloader::PendingRequestSnapshot& request,
	const std::unordered_map< downloader::WorkID, DownloadSessionUrlID >& rows,
	const SteadyClock::time_point now )
{
	builder.start_object();
	builder.append_key_value( "work_id", request.work );
	builder.append_comma();
	builder.escape_and_append_with_quotes( "url_id" );
	builder.append_colon();
	if ( const auto id { urlIdFor( rows, request.work ) } )
		builder.append( *id );
	else
		builder.append_raw( "null" );
	builder.append_comma();
	builder.append_key_value( "url", request.url );
	builder.append_comma();
	builder.append_key_value( "lane", request.lane );
	builder.append_comma();
	builder.append_key_value( "import", request.import );
	builder.append_comma();
	builder.append_key_value( "age_ms", ageMs( request.started_at, now ) );
	builder.end_object();
}

static void appendEvent(
	simdjson::builder::string_builder& builder,
	const downloader::SessionEvent& event,
	const std::unordered_map< downloader::WorkID, DownloadSessionUrlID >& rows )
{
	builder.start_object();
	builder.append_key_value( "sequence", event.sequence );
	builder.append_comma();
	builder.append_key_value(
		"at", std::chrono::duration_cast< std::chrono::microseconds >( event.at.time_since_epoch() ).count() );
	builder.append_comma();
	builder.append_key_value( "kind", downloader::toString( event.kind ) );
	builder.append_comma();
	builder.append_key_value( "work_id", event.work );
	builder.append_comma();
	builder.escape_and_append_with_quotes( "url_id" );
	builder.append_colon();
	if ( const auto id { urlIdFor( rows, event.work ) } )
		builder.append( *id );
	else
		builder.append_raw( "null" );
	builder.append_comma();
	builder.append_key_value( "url", event.url );
	builder.append_comma();
	builder.append_key_value( "detail", event.detail );
	builder.append_comma();
	builder.escape_and_append_with_quotes( "status" );
	builder.append_colon();
	if ( event.status )
		builder.append( *event.status );
	else
		builder.append_raw( "null" );
	builder.append_comma();
	builder.append_key_value( "bytes", static_cast< std::uint64_t >( event.bytes ) );
	builder.append_comma();
	builder.escape_and_append_with_quotes( "record_id" );
	builder.append_colon();
	if ( event.record_id == 0 )
		builder.append_raw( "null" );
	else
		builder.append( event.record_id );
	builder.end_object();
}

static void appendLane( simdjson::builder::string_builder& builder, const downloader::LaneSnapshot& lane )
{
	builder.start_object();
	builder.append_key_value( "scheduling_key", lane.key );
	builder.append_comma();
	builder.append_key_value( "group", lane.group.value_or( "" ) );
	builder.append_comma();
	builder.append_key_value( "throttled", lane.throttled );
	builder.append_comma();
	builder.append_key_value(
		"effective_interval_ms",
		static_cast< std::uint64_t >(
			std::chrono::ceil< std::chrono::milliseconds >( lane.effective_interval ).count() ) );
	builder.append_comma();
	builder.append_key_value(
		"remaining_ms",
		static_cast< std::uint64_t >( std::chrono::ceil< std::chrono::milliseconds >( lane.remaining ).count() ) );
	builder.append_comma();
	builder.append_key_value( "consecutive_limits", lane.consecutive_failures );
	builder.append_comma();
	builder.append_key_value( "backed_off", lane.backed_off );
	builder.append_comma();
	builder.append_key_value( "in_flight", static_cast< std::uint64_t >( lane.in_flight ) );
	builder.append_comma();
	builder.append_key_value( "queued", static_cast< std::uint64_t >( lane.queued ) );
	builder.append_comma();
	builder.append_key_value( "shards", static_cast< std::uint64_t >( lane.shards ) );
	builder.append_comma();
	builder.append_key_value( "bytes_per_second", lane.bytes_per_second );
	builder.append_comma();
	builder.append_key_value( "active", lane.active );
	builder.end_object();
}

[[nodiscard]] static bool parseCursors(
	const std::string_view text,
	std::unordered_map< DownloadSessionID, std::uint64_t >& cursors )
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

struct DownloaderDebugSnapshot
{
	http::ValidatedJson data;
	std::unordered_map< DownloadSessionID, std::uint64_t > cursors;
};

static runtime::Task< DownloaderDebugSnapshot > debugSnapshot(
	db::DBContext& db,
	const std::unordered_map< DownloadSessionID, std::uint64_t >& cursors )
{
	auto infos { downloader::downloadSessionManager().debugSnapshots( cursors ) };

	std::vector< DownloadSessionID > ids {};
	ids.reserve( infos.size() );
	for ( const auto& info : infos ) ids.emplace_back( info.id );

	std::unordered_map< DownloadSessionID, std::string > names {};
	if ( !ids.empty() )
	{
		db::DBBindings bindings {};
		bindings.bind( ids );
		const auto rows {
			co_await db
				.query(
					"SELECT download_session_id, name FROM download_sessions WHERE download_session_id = ANY($1::BIGINT[])",
					std::move( bindings ) )
				.rows< DownloadSessionID, std::string >()
		};

		for ( const auto& [ id, name ] : rows ) names.emplace( id, name );
	}

	const auto now { SteadyClock::now() };

	simdjson::builder::string_builder builder;
	builder.start_object();
	builder.escape_and_append_with_quotes( "sessions" );
	builder.append_colon();
	builder.start_array();

	bool first_session { true };
	for ( const auto& info : infos )
	{
		if ( !first_session ) builder.append_comma();
		first_session = false;

		const auto& snapshot { info.snapshot };
		builder.start_object();
		builder.append_key_value( "id", info.id );
		builder.append_comma();
		builder.escape_and_append_with_quotes( "name" );
		builder.append_colon();
		if ( const auto name { names.find( info.id ) }; name != names.end() )
			builder.append( name->second );
		else
			builder.append_raw( "null" );
		builder.append_comma();
		builder.append_key_value( "root_url", snapshot.root_url );
		builder.append_comma();
		builder.append_key_value( "state", stateOf( snapshot ) );
		builder.append_comma();
		builder.append_key_value( "closed", snapshot.closed );
		builder.append_comma();
		builder.append_key_value( "cancelled", snapshot.cancelled );
		builder.append_comma();
		builder.append_key_value( "idle", snapshot.idle );
		builder.append_comma();
		builder.append_key_value( "queued", static_cast< std::uint64_t >( snapshot.queued ) );
		builder.append_comma();
		builder.append_key_value( "running", static_cast< std::uint64_t >( snapshot.running ) );
		builder.append_comma();
		builder.append_key_value( "in_flight", static_cast< std::uint64_t >( snapshot.in_flight_requests ) );
		builder.append_comma();
		builder.append_key_value( "in_flight_limit", static_cast< std::uint64_t >( snapshot.in_flight_limit ) );
		builder.append_comma();
		builder.append_key_value( "outstanding", static_cast< std::uint64_t >( snapshot.outstanding ) );
		builder.append_comma();
		builder.escape_and_append_with_quotes( "counters" );
		builder.append_colon();
		appendCounters( builder, snapshot.counters );
		builder.append_comma();

		builder.escape_and_append_with_quotes( "work" );
		builder.append_colon();
		builder.start_array();
		bool first_work { true };
		for ( const auto& item : snapshot.work )
		{
			if ( !first_work ) builder.append_comma();
			first_work = false;
			appendWork( builder, item, info.rows, now );
		}
		builder.end_array();
		builder.append_comma();

		builder.escape_and_append_with_quotes( "requests" );
		builder.append_colon();
		builder.start_array();
		bool first_request { true };
		for ( const auto& pending : snapshot.requests )
		{
			if ( !first_request ) builder.append_comma();
			first_request = false;
			appendRequest( builder, pending, info.rows, now );
		}
		builder.end_array();
		builder.append_comma();

		builder.escape_and_append_with_quotes( "events" );
		builder.append_colon();
		builder.start_array();
		bool first_event { true };
		for ( const auto& event : snapshot.events )
		{
			if ( !first_event ) builder.append_comma();
			first_event = false;
			appendEvent( builder, event, info.rows );
		}
		builder.end_array();
		builder.append_comma();

		builder.append_key_value( "event_sequence", snapshot.event_sequence );
		builder.append_comma();
		builder.append_key_value( "events_dropped", snapshot.events_dropped );
		builder.end_object();
	}

	builder.end_array();
	builder.append_comma();

	auto lanes_snapshot { downloader::downloadSessionManager().laneSnapshots() };
	std::ranges::sort( lanes_snapshot, {}, &downloader::LaneSnapshot::key );

	builder.escape_and_append_with_quotes( "lanes" );
	builder.append_colon();
	builder.start_array();
	bool first_lane { true };
	for ( const auto& lane : lanes_snapshot )
	{
		if ( !first_lane ) builder.append_comma();
		first_lane = false;
		appendLane( builder, lane );
	}
	builder.end_array();

	builder.end_object();

	auto json { builder.view() };
	if ( json.error() ) throw std::runtime_error( simdjson::error_message( json.error() ) );

	std::unordered_map< DownloadSessionID, std::uint64_t > next_cursors;
	for ( const auto& info : infos ) next_cursors.emplace( info.id, info.snapshot.event_sequence );
	co_return DownloaderDebugSnapshot {
		http::ValidatedJson::parse( std::string { *json } ).value(), std::move( next_cursors )
	};
}

static http::HTTPTask< http::Response > debug( db::DBContext& db, http::RequestContext context )
{
	const auto event_cursors_e { context.parameters().optional< std::string >( "event_cursors" ) };
	if ( !event_cursors_e ) co_return http::Response::error( 400, event_cursors_e.error().message );

	const auto since_e { context.parameters().optional< std::string >( "since" ) };
	if ( !since_e ) co_return http::Response::error( 400, since_e.error().message );

	if ( event_cursors_e->has_value() && since_e->has_value() )
		co_return http::Response::error( 400, "Supply only one of 'event_cursors' and the deprecated 'since' alias" );

	const auto cursor_text { event_cursors_e->has_value() ? *event_cursors_e : *since_e };
	const auto parameter_name { event_cursors_e->has_value() ? "event_cursors" : "since" };
	std::unordered_map< DownloadSessionID, std::uint64_t > cursors {};
	if ( !parseCursors( cursor_text.value_or( "" ), cursors ) )
		co_return http::Response::error(
			400,
			std::format(
				"The '{}' parameter must be a comma separated list of <session_id>:<event_sequence>",
				parameter_name ) );

	const auto snapshot { co_await debugSnapshot( db, cursors ) };
	co_return http::Response::json( 200, folly::IOBuf::copyBuffer( snapshot.data.text() ) );
}

static runtime::Task< http::ValidatedJson > renderDebug(
	db::DBContext& db,
	std::shared_ptr< std::unordered_map< DownloadSessionID, std::uint64_t > > cursors )
{
	// Batch concurrent session and lane transitions before reading their latest state.
	co_await folly::coro::sleep( std::chrono::milliseconds { 100 } );
	auto snapshot { co_await debugSnapshot( db, *cursors ) };
	*cursors = std::move( snapshot.cursors );
	co_return std::move( snapshot.data );
}

http::RouteDefinition downloaderDebugEventsRoute( db::DBContext& db )
{
	return http::RouteDefinition {
		http::RouteMetadata { proxygen::HTTPMethod::GET, "/downloader/debug/events", {}, { apiAuthValidation( db ) } },
		[ &db ]( [[maybe_unused]] http::RequestContext context ) -> http::HTTPTask< http::Response >
		{
			auto cursors { std::make_shared< std::unordered_map< DownloadSessionID, std::uint64_t > >() };
			http::Response response {};
			response.body = proxygen::coro::HTTPSourceHolder { new http::SSEBodySource {
				std::move( response.message ),
				downloader::debugEvents().subscribe( {}, { http::ValidatedJson::parse( "{}" ).value() }, false ),
				[ &db, cursors ] { return renderDebug( db, cursors ); } } };
			co_return response;
		}
	};
}

http::RouteDefinition downloaderDebugRoute( db::DBContext& db )
{
	auto bound = [ &db ]( http::RequestContext context ) -> http::HTTPTask< http::Response >
	{ co_return co_await debug( db, std::move( context ) ); };

	return http::RouteDefinition {
		http::RouteMetadata { proxygen::HTTPMethod::GET, "/downloader/debug", {}, { apiAuthValidation( db ) } },
		std::move( bound )
	};
}

} // namespace idhan::httpapi
