#include "downloaderEvents.hpp"

#include <simdjson.h>

#include <format>
#include <mutex>
#include <unordered_set>

#include "events/ServerEvents.hpp"
#include "logging/log.hpp"
#include "subscriptions/SubscriptionEventHub.hpp"

namespace idhan::downloader::events
{

class JsonObject
{
	simdjson::builder::string_builder& m_builder;
	bool m_first { true };

	void separate()
	{
		if ( !m_first ) m_builder.append_comma();

		m_first = false;
	}

  public:

	explicit JsonObject( simdjson::builder::string_builder& builder ) : m_builder( builder )
	{
		m_builder.start_object();
	}

	template < typename T >
	void field( const std::string_view key, const T& value )
	{
		separate();
		m_builder.append_key_value( key, value );
	}

	template < typename T >
	void optionalField( const std::string_view key, const std::optional< T >& value )
	{
		if ( !value.has_value() ) return;

		field( key, value.value() );
	}

	void key( const std::string_view name )
	{
		separate();
		m_builder.escape_and_append_with_quotes( name );
		m_builder.append_colon();
	}

	void end() { m_builder.end_object(); }
};

static std::optional< http::ValidatedJson > render( simdjson::builder::string_builder& builder )
{
	auto json { builder.view() };
	if ( json.error() )
	{
		log::error( "Unable to serialize downloader event: {}", simdjson::error_message( json.error() ) );
		return std::nullopt;
	}

	auto validated { http::ValidatedJson::parse( std::string { json.value() } ) };
	if ( !validated.has_value() )
	{
		log::error( "Unable to validate downloader event: {}", validated.error().message );
		return std::nullopt;
	}

	return std::move( validated.value() );
}

static http::ValidatedJson renderOrThrow( simdjson::builder::string_builder& builder )
{
	auto json { render( builder ) };
	if ( !json.has_value() ) throw std::runtime_error( "Unable to serialize a downloader snapshot" );

	return std::move( json.value() );
}

static std::string internalType( const std::string_view type )
{
	return std::format( "downloader.{}", type );
}

static void publish(
	const std::string_view type,
	const DownloadSessionID session_id,
	std::string entity,
	simdjson::builder::string_builder& builder,
	const bool coalesce = false )
{
	auto json { render( builder ) };
	if ( !json.has_value() ) return;

	server::events().publish(
		internalType( type ),
		std::move( entity ),
		std::move( json.value() ),
		http::SSEPublishOptions {
			.retain_snapshot = false, .coalesce = coalesce, .session = std::to_string( session_id ) } );
}

static void appendStateFields( JsonObject& object, const UrlState& state )
{
	object.field( "state", state.state );
	object.optionalField( "record_id", state.record_id );
	object.optionalField( "size", state.size );
	object.optionalField( "stage", state.stage );
	object.optionalField( "error", state.error );
	object.optionalField( "finished_at", state.finished_at );
}

static void appendNetworkFields( JsonObject& object, const SessionSnapshot& snapshot )
{
	object.field( "downloading", static_cast< std::uint64_t >( snapshot.downloading ) );
	object.field( "waiting", static_cast< std::uint64_t >( snapshot.waiting ) );
	object.field( "waiting_parsers", static_cast< std::uint64_t >( snapshot.waiting_parsers ) );
	object.field( "waiting_requests", static_cast< std::uint64_t >( snapshot.waiting_requests ) );
	object.field( "waiting_http_rate", static_cast< std::uint64_t >( snapshot.waiting_http_rate ) );
	object.field( "waiting_http_shard", static_cast< std::uint64_t >( snapshot.waiting_http_shard ) );
	object.field( "waiting_storage", static_cast< std::uint64_t >( snapshot.waiting_storage ) );
	object.field( "downloaded_bytes", snapshot.downloaded_bytes );
	object.field( "average_import_bytes", snapshot.average_import_bytes );
}

static void appendSessionFields( JsonObject& object, const SessionInfo& session )
{
	object.field( "session_id", session.id );
	object.field( "name", session.name );
	object.field( "created_at", session.created_at );
	object.field( "last_used_at", session.last_used_at );
	object.field( "is_subscription", session.is_subscription );
}

http::SSEInitialEvent bandwidthSnapshot( const Bandwidth& speeds )
{
	simdjson::builder::string_builder builder;
	JsonObject object { builder };
	object.field( "type", "bandwidth" );
	object.field( "download_bytes_per_second", speeds.download_bytes_per_second );
	object.key( "sessions" );
	builder.start_array();
	bool first { true };
	for ( const auto& session : speeds.sessions )
	{
		if ( !first ) builder.append_comma();

		first = false;
		JsonObject entry { builder };
		entry.field( "session_id", session.session_id );
		entry.field( "download_bytes_per_second", session.download_bytes_per_second );
		entry.end();
	}
	builder.end_array();
	object.end();
	return { internalType( "bandwidth" ), "bandwidth", renderOrThrow( builder ), {} };
}

void bandwidth( const Bandwidth& speeds )
{
	auto event { bandwidthSnapshot( speeds ) };
	server::events().publish(
		std::move( event.type ),
		std::move( event.entity ),
		std::move( event.data ),
		http::SSEPublishOptions { .retain_snapshot = false, .coalesce = true } );
}

static void appendErrorFields( JsonObject& object, const RequestError& error )
{
	object.field( "error_id", error.error_id );
	object.optionalField( "url_id", error.url_id );
	object.field( "url", error.url );
	object.field( "lane", error.lane );
	object.optionalField( "http_status", error.http_status );
	object.optionalField( "message", error.message );
}

static std::mutex subscription_sessions_mutex;
static std::unordered_set< DownloadSessionID > subscription_sessions;

void setSubscriptionSession( const DownloadSessionID session_id, const bool subscription )
{
	const std::scoped_lock lock { subscription_sessions_mutex };
	if ( subscription )
		subscription_sessions.insert( session_id );
	else
		subscription_sessions.erase( session_id );
}

static void subscriptionRecordsChanged( const DownloadSessionID session_id )
{
	{
		const std::scoped_lock lock { subscription_sessions_mutex };
		if ( !subscription_sessions.contains( session_id ) ) return;
	}

	subscriptions::SubscriptionEventHub::instance().sessionRecordsChanged( session_id );
}

bool interested( const DownloadSessionID session_id )
{
	{
		const std::scoped_lock lock { subscription_sessions_mutex };
		if ( subscription_sessions.contains( session_id ) ) return false;
	}

	return server::events().interested( "downloader", std::to_string( session_id ) );
}

void urlCreated( const DownloadSessionID session_id, const UrlRow& row )
{
	if ( !interested( session_id ) ) return;

	simdjson::builder::string_builder builder;
	JsonObject object { builder };
	object.field( "type", "url_created" );
	object.field( "session_id", session_id );
	object.field( "url_id", row.url_id );
	object.optionalField( "parent_id", row.parent_id );
	object.field( "url", row.url );
	object.field( "created_at", row.created_at );
	// A newly created URL is pending unless it was settled on creation.
	if ( row.state.state != "pending" ) appendStateFields( object, row.state );
	object.end();
	publish( "url_created", session_id, std::to_string( session_id ), builder );
}

void urlUpdated( const DownloadSessionID session_id, const DownloadSessionUrlID url_id, const UrlState& state )
{
	if ( state.record_id.has_value() ) subscriptionRecordsChanged( session_id );

	if ( !interested( session_id ) ) return;

	simdjson::builder::string_builder builder;
	JsonObject object { builder };
	object.field( "type", "url_updated" );
	object.field( "session_id", session_id );
	object.field( "url_id", url_id );
	appendStateFields( object, state );
	object.end();
	publish( "url_updated", session_id, std::to_string( session_id ), builder );
}

void urlRemoved( const DownloadSessionID session_id, const DownloadSessionUrlID url_id )
{
	subscriptionRecordsChanged( session_id );

	if ( !interested( session_id ) ) return;

	simdjson::builder::string_builder builder;
	JsonObject object { builder };
	object.field( "type", "url_removed" );
	object.field( "session_id", session_id );
	object.field( "url_id", url_id );
	object.end();
	publish( "url_removed", session_id, std::to_string( session_id ), builder );
}

void urlProgress(
	const DownloadSessionID session_id,
	const DownloadSessionUrlID url_id,
	const std::uint64_t bytes,
	const std::uint64_t total )
{
	if ( !interested( session_id ) ) return;

	simdjson::builder::string_builder builder;
	JsonObject object { builder };
	object.field( "type", "url_progress" );
	object.field( "session_id", session_id );
	object.field( "url_id", url_id );
	object.field( "bytes", bytes );
	if ( total > 0 ) object.field( "total", total );
	object.end();
	// Only the newest progress of a URL matters to a client that has fallen behind.
	publish( "url_progress", session_id, std::format( "{}:{}", session_id, url_id ), builder, true );
}

void requestError( const DownloadSessionID session_id, const RequestError& error )
{
	if ( !interested( session_id ) ) return;

	simdjson::builder::string_builder builder;
	JsonObject object { builder };
	object.field( "type", "request_error" );
	object.field( "session_id", session_id );
	appendErrorFields( object, error );
	object.end();
	publish( "request_error", session_id, std::to_string( session_id ), builder );
}

void requestErrorsCleared( const DownloadSessionID session_id, const std::vector< std::int64_t >& error_ids )
{
	if ( error_ids.empty() ) return;

	if ( !interested( session_id ) ) return;

	simdjson::builder::string_builder builder;
	JsonObject object { builder };
	object.field( "type", "request_errors_cleared" );
	object.field( "session_id", session_id );
	object.key( "error_ids" );
	builder.start_array();
	bool first { true };
	for ( const auto error_id : error_ids )
	{
		if ( !first ) builder.append_comma();

		first = false;
		builder.append( error_id );
	}
	builder.end_array();
	object.end();
	publish( "request_errors_cleared", session_id, std::to_string( session_id ), builder );
}

void sessionCreated( const SessionInfo& session )
{
	setSubscriptionSession( session.id, session.is_subscription );
	if ( !interested( session.id ) ) return;

	simdjson::builder::string_builder builder;
	JsonObject object { builder };
	object.field( "type", "session_created" );
	appendSessionFields( object, session );
	object.end();
	publish( "session_created", session.id, std::to_string( session.id ), builder );
}

void sessionState(
	const DownloadSessionID session_id,
	const std::string_view state,
	const std::optional< std::int64_t > last_used_at )
{
	if ( !interested( session_id ) ) return;

	simdjson::builder::string_builder builder;
	JsonObject object { builder };
	object.field( "type", "session_state" );
	object.field( "session_id", session_id );
	object.field( "state", state );
	object.optionalField( "last_used_at", last_used_at );
	object.end();
	publish( "session_state", session_id, std::to_string( session_id ), builder );
}

void sessionDeleted( const DownloadSessionID session_id )
{
	if ( !interested( session_id ) ) return;

	simdjson::builder::string_builder builder;
	JsonObject object { builder };
	object.field( "type", "session_deleted" );
	object.field( "session_id", session_id );
	object.end();
	publish( "session_deleted", session_id, std::to_string( session_id ), builder );
}

void network( const DownloadSessionID session_id, const SessionSnapshot& snapshot )
{
	if ( !interested( session_id ) ) return;

	simdjson::builder::string_builder builder;
	JsonObject object { builder };
	object.field( "type", "network" );
	object.field( "session_id", session_id );
	appendNetworkFields( object, snapshot );
	object.end();
	publish( "network", session_id, std::to_string( session_id ), builder, true );
}

http::SSEInitialEvent sessionSnapshot( const SessionInfo& session, const std::string_view state )
{
	simdjson::builder::string_builder builder;
	JsonObject object { builder };
	object.field( "type", "session_snapshot" );
	appendSessionFields( object, session );
	object.field( "state", state );
	object.end();
	return http::SSEInitialEvent {
		internalType( "session_snapshot" ), std::to_string( session.id ), renderOrThrow( builder ),
		std::to_string( session.id )
	};
}

http::SSEInitialEvent urlsSnapshot( const DownloadSessionID session_id, const std::span< const UrlRow > rows )
{
	simdjson::builder::string_builder builder;
	JsonObject object { builder };
	object.field( "type", "urls_snapshot" );
	object.field( "session_id", session_id );
	object.key( "urls" );
	builder.start_array();
	bool first { true };
	for ( const auto& row : rows )
	{
		if ( !first ) builder.append_comma();

		first = false;
		JsonObject entry { builder };
		entry.field( "url_id", row.url_id );
		entry.optionalField( "parent_id", row.parent_id );
		entry.field( "url", row.url );
		entry.field( "created_at", row.created_at );
		appendStateFields( entry, row.state );
		entry.optionalField( "bytes", row.bytes );
		entry.optionalField( "total", row.total );
		entry.end();
	}
	builder.end_array();
	object.end();
	return http::SSEInitialEvent {
		internalType( "urls_snapshot" ), std::to_string( session_id ), renderOrThrow( builder ),
		std::to_string( session_id )
	};
}

http::SSEInitialEvent errorsSnapshot( const DownloadSessionID session_id, const std::span< const RequestError > errors )
{
	simdjson::builder::string_builder builder;
	JsonObject object { builder };
	object.field( "type", "errors_snapshot" );
	object.field( "session_id", session_id );
	object.key( "errors" );
	builder.start_array();
	bool first { true };
	for ( const auto& error : errors )
	{
		if ( !first ) builder.append_comma();

		first = false;
		JsonObject entry { builder };
		appendErrorFields( entry, error );
		entry.field( "occurred_at", error.occurred_at );
		entry.end();
	}
	builder.end_array();
	object.end();
	return http::SSEInitialEvent {
		internalType( "errors_snapshot" ), std::to_string( session_id ), renderOrThrow( builder ),
		std::to_string( session_id )
	};
}

http::SSEInitialEvent networkSnapshot( const DownloadSessionID session_id, const SessionSnapshot& snapshot )
{
	simdjson::builder::string_builder builder;
	JsonObject object { builder };
	object.field( "type", "network" );
	object.field( "session_id", session_id );
	appendNetworkFields( object, snapshot );
	object.end();
	return http::SSEInitialEvent {
		internalType( "network" ), std::to_string( session_id ), renderOrThrow( builder ),
		std::to_string( session_id )
	};
}

http::SSEInitialEvent snapshotComplete()
{
	return http::SSEInitialEvent { internalType( "snapshot_complete" ),
		                           {},
		                           http::ValidatedJson::parse( R"({"type":"snapshot_complete"})" ).value() };
}

} // namespace idhan::downloader::events
