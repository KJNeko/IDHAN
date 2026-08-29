#include "SessionContext.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <utility>

#include "URLUtils.hpp"
#include "http/HttpMessage.hpp"
#include "js/ScriptContext.hpp"
#include "js/bindings/IdhanBindings.hpp"
#include "logging/format_ns.hpp"

namespace idhan::downloader
{

enum class PromiseOutcome : std::uint8_t
{
	PENDING,
	FULFILLED,
	REJECTED,
};

static PromiseOutcome outcomeOf( JSContext* context, const JSValueConst value )
{
	switch ( JS_PromiseState( context, value ) )
	{
		case JS_PROMISE_PENDING:
			return PromiseOutcome::PENDING;
		case JS_PROMISE_REJECTED:
			return PromiseOutcome::REJECTED;
		case JS_PROMISE_FULFILLED:
			return PromiseOutcome::FULFILLED;
	}

	return PromiseOutcome::FULFILLED;
}

static std::string rejectionOf( JSContext* context, const JSValueConst promise )
{
	JSValue reason { JS_PromiseResult( context, promise ) };
	std::string message { scriptErrorString( context, reason ) };
	JS_FreeValue( context, reason );
	return message;
}

SessionContext::Impl::Impl( SessionEnvironment environment, SessionOptions options ) :
  m_environment( std::move( environment ) ),
  m_options( std::move( options ) )
{
	if ( m_options.inflight_requests == 0 )
		m_options.inflight_requests = m_environment.config.session_inflight_requests;
}

SessionContext::Impl::~Impl()
{
	close();
}

void SessionContext::Impl::adopt( const std::shared_ptr< SessionContext >& self )
{
	m_self = self;
}

std::expected< WorkID, std::string > SessionContext::Impl::submit(
	std::string url,
	std::optional< WorkID > parent,
	std::function< void( WorkID ) > on_reserved )
{
	if ( m_cancelled.load() || m_closed.load() ) return std::unexpected( "The download session is closed" );

	auto route { m_environment.registry->route( url ) };

	if ( !route ) return std::unexpected( std::move( route.error() ) );
	if ( !route->has_value() ) return std::unexpected( format_ns::format( "No URL class accepts URL: {}", url ) );

	const WorkID id { m_next_work.fetch_add( 1 ) };

	if ( on_reserved ) on_reserved( id );

	if ( !enqueue( id, parent, std::move( url ) ) ) return std::unexpected( "The download session is closed" );

	return id;
}

bool SessionContext::Impl::enqueue( const WorkID id, const std::optional< WorkID > parent, std::string url )
{
	auto handle { m_self.lock() };

	if ( !handle || m_environment.runner == nullptr ) return false;

	{
		const std::scoped_lock lock { m_mutex };
		m_seen.emplace( url );
		++m_queued;
		m_outstanding.fetch_add( 1 );
		m_drained_reported = false;
	}

	// Avoid lock inversion with the runner queue.
	const bool queued { m_environment.runner->submit(
		ScriptWork { .handle = std::move( handle ), .id = id, .parent = parent, .url = std::move( url ) } ) };

	if ( !queued )
	{
		retireQueued( id );
		return false;
	}

	publishDiagnostics();
	return true;
}

void SessionContext::Impl::cancel()
{
	m_cancelled.store( true );
	m_environment.lanes->cancel( m_transfer_cancelled );

	if ( const auto handle { m_self.lock() }; handle && m_environment.runner != nullptr )
		m_environment.runner->discard( handle.get() );
}

void SessionContext::Impl::close()
{
	if ( m_closed.exchange( true ) ) return;

	if ( const auto handle { m_self.lock() }; handle && m_environment.runner != nullptr )
		m_environment.runner->discard( handle.get() );

	m_drained.notify_all();
}

void SessionContext::Impl::wait()
{
	std::unique_lock lock { m_mutex };
	m_drained.wait( lock, [ this ] { return m_closed.load() || ( m_outstanding.load() == 0 && m_pending.empty() ); } );
}

bool SessionContext::Impl::idle() const
{
	const std::scoped_lock lock { m_mutex };
	return m_outstanding.load() == 0 && m_pending.empty();
}

std::size_t SessionContext::Impl::outstanding() const
{
	return m_outstanding.load();
}

bool SessionContext::Impl::reserveStart()
{
	if ( m_cancelled.load() || m_closed.load() ) return false;

	const std::scoped_lock lock { m_mutex };

	if ( m_inflight_requests + m_starting >= m_options.inflight_requests ) return false;

	++m_starting;
	return true;
}

bool SessionContext::Impl::acceptsWork() const
{
	return !m_cancelled.load() && !m_closed.load();
}

void SessionContext::Impl::releaseStart()
{
	const std::scoped_lock lock { m_mutex };

	if ( m_starting > 0 ) --m_starting;
}

void SessionContext::Impl::retire( const WorkID id )
{
	{
		const std::scoped_lock lock { m_mutex };
		m_records.erase( id );
		m_outstanding.fetch_sub( 1 );
	}

	publishDiagnostics();
	reportDrainedIfIdle();
}

void SessionContext::Impl::reportDrainedIfIdle()
{
	bool drained {};

	{
		const std::scoped_lock lock { m_mutex };

		if ( m_outstanding.load() != 0 || !m_pending.empty() || m_drained_reported ) return;

		m_drained_reported = true;
		drained = true;
	}

	if ( !drained ) return;

	m_drained.notify_all();
	m_diagnostics.recordFinished();

	if ( m_options.observer != nullptr ) m_options.observer->onFinished();
}

void SessionContext::Impl::retireQueued( const WorkID id )
{
	{
		const std::scoped_lock lock { m_mutex };

		if ( m_queued > 0 ) --m_queued;
	}

	retire( id );
}

void SessionContext::Impl::publishDiagnostics()
{
	SessionDiagnostics::LoopState state {};

	{
		const std::scoped_lock lock { m_mutex };
		state.queued = m_queued;
		state.in_flight_requests = m_inflight_requests;
		state.work.reserve( m_records.size() );
		state.requests.reserve( m_pending.size() );

		for ( const auto& [ id, record ] : m_records )
		{
			state.work.emplace_back(
				WorkSnapshot {
					.id = id,
					.parent = record.parent,
					.url = record.url,
					.url_class = record.url_class,
					.parser = record.parser,
					.phase = record.phase,
					.outstanding = record.outstanding,
					.started_at = record.started_at } );
		}

		for ( const auto& [ id, pending ] : m_pending )
		{
			state.requests.emplace_back(
				PendingRequestSnapshot {
					.work = pending.work,
					.url = pending.url,
					.lane = pending.lane,
					.import = pending.import,
					.started_at = pending.started_at } );
		}
	}

	std::ranges::sort(
		state.work, []( const WorkSnapshot& left, const WorkSnapshot& right ) { return left.id < right.id; } );
	m_diagnostics.publish( std::move( state ) );
}

SessionSnapshot SessionContext::Impl::snapshot( const std::uint64_t since ) const
{
	SessionSnapshot snapshot { m_diagnostics.snapshot( since ) };
	snapshot.root_url = m_options.root_url;
	snapshot.closed = m_closed.load();
	snapshot.cancelled = m_cancelled.load();
	snapshot.outstanding = m_outstanding.load();
	snapshot.in_flight_limit = m_options.inflight_requests;
	snapshot.idle = snapshot.outstanding == 0 && snapshot.in_flight_requests == 0;
	return snapshot;
}

WorkInfo SessionContext::Impl::infoFor( const ScriptExecution& execution )
{
	return WorkInfo { .id = execution.work_id, .parent = execution.parent_id, .url = execution.parser_url };
}

static WorkPhase phaseOf( const ScriptExecution& execution )
{
	if ( execution.settled ) return WorkPhase::TRANSFERRING;

	switch ( execution.stage )
	{
		case ScriptStage::MODULE:
			return WorkPhase::MODULE;
		case ScriptStage::PARSER:
			return WorkPhase::PARSER;
		case ScriptStage::DONE:
			return WorkPhase::TRANSFERRING;
	}

	return WorkPhase::TRANSFERRING;
}

std::unique_ptr< ScriptExecution > SessionContext::Impl::beginWork(
	ScriptContext& scripts,
	ScriptWork& work,
	const std::size_t worker )
{
	{
		const std::scoped_lock lock { m_mutex };

		if ( m_queued > 0 ) --m_queued;
	}

	auto route { m_environment.registry->route( work.url ) };

	auto execution { std::make_unique< ScriptExecution >() };
	execution->session = this;
	execution->worker = worker;
	execution->work_id = work.id;
	execution->parent_id = work.parent;
	execution->parser_url = work.url;

	const WorkInfo info { infoFor( *execution ) };

	const auto abort = [ & ]( const std::string& error )
	{
		spdlog::warn( "downloader session: work {} could not start: {}", work.id, error );
		m_diagnostics.recordFailed( info, error );

		if ( m_options.observer != nullptr ) m_options.observer->onFailed( info, error );

		retire( work.id );
	};

	if ( !route || !route->has_value() )
	{
		abort( route ? format_ns::format( "No URL class accepts URL: {}", work.url ) : std::move( route.error() ) );
		return {};
	}

	execution->route = std::move( **route );

	auto realm { scripts.createRealm( *execution ) };

	if ( !realm )
	{
		abort( realm.error() );
		return {};
	}

	execution->context = *realm;

	{
		const std::scoped_lock lock { m_mutex };
		m_records.insert_or_assign(
			work.id,
			WorkRecord {
				.parent = work.parent,
				.url = work.url,
				.url_class = execution->route.url_class,
				.parser = execution->route.export_name,
				.phase = WorkPhase::MODULE,
				.outstanding = 0,
				.started_at = std::chrono::steady_clock::now() } );
	}

	m_diagnostics.recordStarted( info );

	if ( m_options.observer != nullptr ) m_options.observer->onStarted( info );

	auto evaluated { scripts.evaluate( *execution, execution->route.script ) };

	if ( !evaluated )
	{
		const std::string error { evaluated.error() };
		abandon( *execution );
		scripts.freeRealm( execution->context );
		abort( error );
		return {};
	}

	execution->result = *evaluated;
	execution->stage = ScriptStage::MODULE;
	publishDiagnostics();
	return execution;
}

bool SessionContext::Impl::step( ScriptContext& scripts, ScriptExecution& execution )
{
	const WorkPhase before { phaseOf( execution ) };

	if ( execution.stage != ScriptStage::DONE )
	{
		const PromiseOutcome outcome { outcomeOf( execution.context, execution.result ) };

		if ( outcome == PromiseOutcome::PENDING ) return false;

		if ( outcome == PromiseOutcome::REJECTED )
		{
			failWork( execution, rejectionOf( execution.context, execution.result ) );
		}
		else if ( execution.stage == ScriptStage::MODULE )
		{
			callParser( scripts, execution );

			if ( phaseOf( execution ) != before )
			{
				const std::scoped_lock lock { m_mutex };

				if ( const auto found { m_records.find( execution.work_id ) }; found != m_records.end() )
					found->second.phase = phaseOf( execution );
			}

			return false;
		}
		else
		{
			execution.stage = ScriptStage::DONE;
		}
	}

	if ( !execution.failed ) finishWork( execution );

	if ( phaseOf( execution ) != before )
	{
		const std::scoped_lock lock { m_mutex };

		if ( const auto found { m_records.find( execution.work_id ) }; found != m_records.end() )
			found->second.phase = phaseOf( execution );
	}

	return execution.outstanding == 0;
}

void SessionContext::Impl::callParser( ScriptContext& scripts, ScriptExecution& execution )
{
	JSContext* context { execution.context };
	JS_FreeValue( context, execution.result );
	execution.result = JS_UNDEFINED;
	execution.stage = ScriptStage::PARSER;

	execution.module_namespace = JS_GetModuleNamespace( context, execution.module );

	if ( JS_IsException( execution.module_namespace ) )
	{
		execution.module_namespace = JS_UNDEFINED;
		failWork( execution, scriptExceptionString( context ) );
		return;
	}

	loadManifestCookies( execution );

	JSValue parser { JS_GetPropertyStr( context, execution.module_namespace, execution.route.export_name.c_str() ) };

	if ( !JS_IsFunction( context, parser ) )
	{
		JS_FreeValue( context, parser );
		failWork(
			execution,
			format_ns::format(
				"{} does not export '{}'", execution.route.script.filename().string(), execution.route.export_name ) );
		return;
	}

	JSValue input { JS_NewObject( context ) };
	JS_SetPropertyStr( context, input, "url", JS_NewString( context, execution.parser_url.c_str() ) );
	JS_SetPropertyStr( context, input, "flags", buildFlags( execution ) );

	JSValue global { JS_GetGlobalObject( context ) };
	JSValue idhan { JS_GetPropertyStr( context, global, "idhan" ) };
	JSValue arguments[] { input, idhan };

	scripts.enterBurst();
	JSValue result { JS_Call( context, parser, JS_UNDEFINED, 2, arguments ) };
	scripts.leaveBurst();

	JS_FreeValue( context, idhan );
	JS_FreeValue( context, global );
	JS_FreeValue( context, input );
	JS_FreeValue( context, parser );

	if ( JS_IsException( result ) )
	{
		JS_FreeValue( context, result );
		failWork( execution, scriptExceptionString( context ) );
		return;
	}

	execution.result = result;
}

void SessionContext::Impl::finishWork( ScriptExecution& execution )
{
	if ( execution.settled ) return;

	execution.settled = true;
	spdlog::debug( "downloader session: work {} finished {}", execution.work_id, execution.parser_url );

	m_diagnostics.recordCompleted( infoFor( execution ) );

	if ( m_options.observer != nullptr ) m_options.observer->onCompleted( infoFor( execution ) );
}

void SessionContext::Impl::failWork( ScriptExecution& execution, std::string error )
{
	if ( execution.failed ) return;

	execution.failed = true;
	execution.settled = true;
	execution.stage = ScriptStage::DONE;
	execution.error = std::move( error );

	const std::string identity { format_ns::format(
		"URL class '{}', parser {} export '{}'",
		execution.route.url_class,
		execution.route.script.filename().string(),
		execution.route.export_name ) };

	spdlog::warn(
		"downloader session: work {} on {} failed: {}", execution.work_id, execution.parser_url, execution.error );

	const std::string reported { format_ns::format( "{}:\n{}", identity, execution.error ) };
	m_diagnostics.recordFailed( infoFor( execution ), reported );

	if ( m_options.observer != nullptr ) m_options.observer->onFailed( infoFor( execution ), reported );
}

void SessionContext::Impl::releaseWork( ScriptContext& scripts, ScriptExecution& execution )
{
	JS_FreeValue( execution.context, execution.result );
	JS_FreeValue( execution.context, execution.module_namespace );
	scripts.freeRealm( execution.context );
	execution.context = nullptr;
	scripts.collectGarbage();
	retire( execution.work_id );
}

void SessionContext::Impl::abandon( ScriptExecution& execution )
{
	std::vector< Pending > taken {};

	{
		const std::scoped_lock lock { m_mutex };

		for ( auto entry { m_pending.begin() }; entry != m_pending.end(); )
		{
			if ( entry->second.execution != &execution )
			{
				++entry;
				continue;
			}

			taken.emplace_back( std::move( entry->second ) );
			entry = m_pending.erase( entry );

			if ( m_inflight_requests > 0 ) --m_inflight_requests;
		}
	}

	for ( Pending& pending : taken ) pending.promise.discard();

	execution.outstanding = 0;
}

FollowResult SessionContext::Impl::follow( ScriptExecution& execution, std::string url )
{
	WorkInfo target { .id = 0, .parent = execution.work_id, .url = url };

	const auto report = [ & ]( const FollowStatus status )
	{
		m_diagnostics.recordFollowed( target, status );

		if ( m_options.observer != nullptr ) m_options.observer->onFollowed( target, status );
	};

	if ( m_cancelled.load() || m_closed.load() )
	{
		report( FollowStatus::FILTERED );
		return FollowResult { .status = FollowStatus::FILTERED };
	}

	// Report outside the lock because observers may request a snapshot.
	bool seen {};

	{
		const std::scoped_lock lock { m_mutex };
		seen = m_seen.contains( url );
	}

	if ( seen )
	{
		report( FollowStatus::ALREADY_EXPLORED );
		return FollowResult { .status = FollowStatus::ALREADY_EXPLORED };
	}

	if ( m_options.observer != nullptr )
	{
		if ( const auto record { m_options.observer->alreadyImported( url ) }; record.has_value() )
		{
			{
				const std::scoped_lock lock { m_mutex };
				m_seen.emplace( url );
			}

			report( FollowStatus::ALREADY_IMPORTED );
			return FollowResult {
				.status = FollowStatus::ALREADY_IMPORTED, .record_id = static_cast< RecordID >( *record )
			};
		}
	}

	auto route { m_environment.registry->route( url ) };

	if ( !route || !route->has_value() )
	{
		report( FollowStatus::FILTERED );
		return FollowResult { .status = FollowStatus::FILTERED };
	}

	bool claimed {};

	{
		// Insertion atomically claims the URL against concurrent followers.
		const std::scoped_lock lock { m_mutex };
		claimed = m_seen.insert( url ).second;
	}

	if ( !claimed )
	{
		report( FollowStatus::ALREADY_EXPLORED );
		return FollowResult { .status = FollowStatus::ALREADY_EXPLORED };
	}

	const WorkID id { m_next_work.fetch_add( 1 ) };
	target.id = id;

	report( FollowStatus::QUEUED );

	if ( !enqueue( id, execution.work_id, std::move( url ) ) )
	{
		const std::string error { "The download session closed before followed work could be queued" };
		m_diagnostics.recordFailed( target, error );

		if ( m_options.observer != nullptr ) m_options.observer->onFailed( target, error );

		return FollowResult { .status = FollowStatus::FILTERED };
	}

	return FollowResult { .status = FollowStatus::QUEUED, .work_id = id };
}

std::optional< std::string > SessionContext::Impl::secret( const std::string_view name )
{
	if ( m_environment.secrets == nullptr ) return std::nullopt;

	return m_environment.secrets->secret( name );
}

std::uint64_t SessionContext::Impl::track( ScriptExecution& execution, Pending pending )
{
	pending.work = execution.work_id;
	pending.worker = execution.worker;
	pending.started_at = std::chrono::steady_clock::now();

	if ( !pending.import )
	{
		pending.execution = &execution;
		++execution.outstanding;
	}

	const std::scoped_lock lock { m_mutex };
	const std::uint64_t id { m_next_pending++ };
	++m_inflight_requests;

	if ( !pending.import )
	{
		if ( const auto found { m_records.find( execution.work_id ) }; found != m_records.end() )
			found->second.outstanding = execution.outstanding;
	}

	m_pending.emplace( id, std::move( pending ) );
	return id;
}

void SessionContext::Impl::deliver( const std::uint64_t id, TransferResult result )
{
	auto handle { m_self.lock() };

	if ( !handle || m_environment.runner == nullptr ) return;

	std::size_t worker {};

	{
		const std::scoped_lock lock { m_mutex };
		const auto found { m_pending.find( id ) };

		if ( found == m_pending.end() ) return;

		found->second.result = std::move( result );
		worker = found->second.worker;
	}

	if ( m_environment.runner->complete( worker, ScriptCompletion { .handle = std::move( handle ), .pending = id } ) )
		return;

	// No worker can settle this after the runner stops.
	{
		const std::scoped_lock lock { m_mutex };

		if ( m_pending.erase( id ) != 0 && m_inflight_requests > 0 ) --m_inflight_requests;
	}

	reportDrainedIfIdle();
}

void SessionContext::Impl::settle( const std::uint64_t id )
{
	Pending pending {};

	{
		const std::scoped_lock lock { m_mutex };
		const auto found { m_pending.find( id ) };

		if ( found == m_pending.end() ) return;

		pending = std::move( found->second );
		m_pending.erase( found );

		if ( m_inflight_requests > 0 ) --m_inflight_requests;
	}

	if ( pending.execution != nullptr && pending.execution->outstanding > 0 ) --pending.execution->outstanding;

	if ( pending.execution != nullptr )
	{
		const std::scoped_lock lock { m_mutex };

		if ( const auto found { m_records.find( pending.work ) }; found != m_records.end() )
			found->second.outstanding = pending.execution->outstanding;
	}

	if ( pending.import )
		settleImport( pending );
	else
		settleRequest( pending );

	publishDiagnostics();
	reportDrainedIfIdle();
}

void SessionContext::Impl::startRequest( ScriptExecution& execution, Json::Value options, PendingPromise promise )
{
	std::vector< std::string > sensitive_query {};
	const Json::Value& sensitive { options[ "sensitiveQuery" ] };

	if ( !sensitive.isNull() )
	{
		if ( !sensitive.isArray() )
		{
			JSValue error { JS_NewError( promise.context ) };
			JS_SetPropertyStr(
				promise.context,
				error,
				"message",
				JS_NewString( promise.context, "request sensitiveQuery must be an array of strings" ) );
			promise.settle( false, error );
			return;
		}

		for ( const Json::Value& name : sensitive )
		{
			if ( !name.isString() )
			{
				JSValue error { JS_NewError( promise.context ) };
				JS_SetPropertyStr(
					promise.context,
					error,
					"message",
					JS_NewString( promise.context, "request sensitiveQuery must contain only strings" ) );
				promise.settle( false, error );
				return;
			}

			sensitive_query.emplace_back( name.asString() );
		}
	}

	std::string response_type {};
	auto request { buildRequest( options, response_type ) };

	if ( !request )
	{
		JSValue error { JS_NewError( promise.context ) };
		JS_SetPropertyStr(
			promise.context, error, "message", JS_NewString( promise.context, request.error().c_str() ) );
		promise.settle( false, error );
		return;
	}

	const std::string url { request->url };
	const std::string recorded_url { detail::redactUrlQuery( url, sensitive_query ) };
	request->sensitive_query = sensitive_query;
	request->cancellation = m_transfer_cancelled;

	const std::uint64_t id { track(
		execution,
		Pending { .promise = promise,
		          .url = recorded_url,
		          .lane = m_environment.lanes->laneKeyForUrl( url ),
		          .response_type = std::move( response_type ),
		          .sensitive_query = std::move( sensitive_query ),
		          .import = false } ) };

	spdlog::debug( "downloader session: work {} requesting {}", execution.work_id, recorded_url );

	auto handle { m_self.lock() };
	m_environment.lanes->send(
		std::move( *request ),
		[ handle = std::move( handle ), id ]( TransferResult result )
		{
			if ( handle ) handle->impl().deliver( id, std::move( result ) );
		} );

	publishDiagnostics();
}

void SessionContext::Impl::startImport( ScriptExecution& execution, Json::Value options )
{
	std::string response_type {};
	auto request { buildRequest( options[ "request" ], response_type ) };

	if ( !request )
	{
		reportImportFailure( execution.work_id, options[ "request" ][ "url" ].asString(), request.error() );
		return;
	}

	if ( m_environment.imports == nullptr )
	{
		reportImportFailure( execution.work_id, request->url, "This downloader host cannot import files" );
		return;
	}

	Json::Value import_options { options };
	import_options.removeMember( "request" );

	ImportRequest import {
		.work = execution.work_id,
		.url = request->url,
		.source_url = execution.parser_url,
		.options = import_options,
		.host_tag = m_options.host_tag
	};

	auto sink { m_environment.imports->open( import ) };

	if ( !sink )
	{
		reportImportFailure( execution.work_id, request->url, sink.error() );
		return;
	}

	request->sink = std::move( *sink );
	request->import = std::move( import );
	request->cancellation = m_transfer_cancelled;

	const std::string url { request->url };

	const std::uint64_t id { track(
		execution,
		Pending { .url = url,
		          .lane = m_environment.lanes->laneKeyForUrl( url ),
		          .import_options = std::move( import_options ),
		          .import = true } ) };

	spdlog::debug( "downloader session: work {} queued import {}", execution.work_id, url );

	auto handle { m_self.lock() };
	m_environment.lanes->send(
		std::move( *request ),
		[ handle = std::move( handle ), id ]( TransferResult result )
		{
			if ( handle ) handle->impl().deliver( id, std::move( result ) );
		} );

	publishDiagnostics();
}

void SessionContext::Impl::rejectPending( Pending& pending, const std::string& error )
{
	JSContext* context { pending.promise.context };

	if ( context == nullptr ) return;

	JSValue value { JS_NewError( context ) };
	JS_SetPropertyStr( context, value, "message", JS_NewString( context, error.c_str() ) );
	pending.promise.settle( false, value );
}

void SessionContext::Impl::reportImportFailure( const WorkID work, const std::string& url, const std::string& error )
{
	spdlog::warn( "downloader session: work {} import of {} failed: {}", work, url, error );
	m_diagnostics.recordImportFailed( work, url, error );

	if ( m_options.observer != nullptr )
		m_options.observer->onImportFailed( WorkInfo { .id = work, .url = url }, url, error );
}

void SessionContext::Impl::settleRequest( Pending& pending )
{
	JSContext* context { pending.promise.context };

	if ( context == nullptr ) return;

	TransferResult& result { pending.result };

	if ( !result )
	{
		rejectPending( pending, format_ns::format( "{}: {}", pending.url, result.error().message ) );
		return;
	}

	TransferResponse& response { *result };

	const RequestInfo request_info {
		.work = pending.work,
		.url = detail::redactUrlQuery( response.url, pending.sensitive_query ),
		.lane = pending.lane,
		.status = response.status,
		.bytes = response.bytes
	};
	m_diagnostics.recordRequest( request_info );

	if ( m_options.observer != nullptr ) m_options.observer->onRequest( request_info );

	JSValue body { JS_UNDEFINED };

	if ( pending.response_type == "json" )
		body = JS_ParseJSON( context, response.body.data(), response.body.size(), response.url.c_str() );
	else if ( pending.response_type == "bytes" || pending.response_type == "fetch" )
		body = JS_NewArrayBufferCopy(
			context, reinterpret_cast< const std::uint8_t* >( response.body.data() ), response.body.size() );
	else
		body = JS_NewStringLen( context, response.body.data(), response.body.size() );

	if ( JS_IsException( body ) )
	{
		const std::string error { scriptExceptionString( context ) };
		const std::string_view content_type { response.headers.get( "content-type" ) };
		JS_FreeValue( context, body );
		rejectPending(
			pending,
			format_ns::format(
				"{} ({} answered {} with {}: {})",
				error,
				request_info.url,
				response.status,
				content_type.empty() ? "no content-type" : content_type,
				describeBody( response.body, response.bytes ) ) );
		return;
	}

	JSValue output { JS_NewObject( context ) };
	JSValue headers { JS_NewObject( context ) };

	for ( const auto& [ name, value ] : response.headers )
		JS_SetPropertyStr( context, headers, name.c_str(), JS_NewStringLen( context, value.data(), value.size() ) );

	JS_SetPropertyStr( context, output, "url", JS_NewString( context, response.url.c_str() ) );
	JS_SetPropertyStr( context, output, "status", JS_NewInt32( context, response.status ) );
	JS_SetPropertyStr( context, output, "headers", headers );
	JS_SetPropertyStr( context, output, "body", body );

	if ( pending.response_type == "fetch" )
		JS_SetPropertyStr(
			context, output, "bodyText", JS_NewStringLen( context, response.body.data(), response.body.size() ) );

	pending.promise.settle( true, output );
}

void SessionContext::Impl::settleImport( Pending& pending )
{
	TransferResult& result { pending.result };

	if ( !result )
	{
		reportImportFailure(
			pending.work, pending.url, format_ns::format( "{}: {}", pending.url, result.error().message ) );
		return;
	}

	TransferResponse& response { *result };

	if ( !response.import.has_value() )
	{
		reportImportFailure(
			pending.work,
			pending.url,
			format_ns::format( "{} returned status {} instead of a file", pending.url, response.status ) );
		return;
	}

	const ImportMetadata& metadata { response.import_metadata };

	const ImportInfo import_info {
		.work = pending.work,
		.url = response.url,
		.record_id = response.import->record_id,
		.size = metadata.size,
		.content_type = metadata.content_type,
		.filename = metadata.filename,
		.note = response.import->note
	};
	m_diagnostics.recordImported( import_info );

	if ( m_options.observer != nullptr ) m_options.observer->onImported( import_info );
}

std::expected< TransferRequest, std::string > SessionContext::Impl::buildRequest(
	const Json::Value& options,
	std::string& response_type )
{
	TransferRequest request {};
	request.url = options[ "url" ].isString() ? options[ "url" ].asString() : std::string {};

	if ( request.url.empty() ) return std::unexpected( "A request needs a url" );

	const std::string method { options[ "method" ].isString() ? options[ "method" ].asString() : "GET" };
	const auto parsed { parseHttpMethod( method ) };

	if ( !parsed ) return std::unexpected( format_ns::format( "Unsupported request method: {}", method ) );

	request.method = *parsed;

	if ( options[ "body" ].isString() ) request.body = options[ "body" ].asString();

	if ( const auto& headers { options[ "headers" ] }; headers.isObject() )
	{
		for ( const auto& name : headers.getMemberNames() )
		{
			if ( !headers[ name ].isString() ) continue;

			const std::string value { headers[ name ].asString() };

			if ( !isValidHttpHeaderName( name ) || !isValidHttpHeaderValue( value ) )
				return std::unexpected( format_ns::format( "Invalid request header: {}", name ) );

			request.headers.add( name, value );
		}
	}

	if ( options[ "referer" ].isString() && !request.headers.contains( "referer" ) )
		request.headers.set( "Referer", options[ "referer" ].asString() );

	response_type = options[ "responseType" ].isString() ? options[ "responseType" ].asString() : "text";

	if ( response_type != "text" && response_type != "json" && response_type != "bytes" && response_type != "fetch" )
		return std::unexpected( format_ns::format( "Unsupported idhan.request responseType: {}", response_type ) );

	// Manual mode exposes each hop so the fetch polyfill can run its redirect algorithm.
	request.options.follow_redirects =
		!( options[ "redirect" ].isString() && options[ "redirect" ].asString() == "manual" );

	if ( !options[ "omitCredentials" ].asBool() )
	{
		request.cookies.overlay = &m_overlay;
		request.cookies.store = m_environment.cookies;
	}

	return request;
}

JSValue SessionContext::Impl::buildFlags( ScriptExecution& execution ) const
{
	JSContext* context { execution.context };
	JSValue flags { JS_NewObject( context ) };
	JSValue manifest { JS_GetPropertyStr( context, execution.module_namespace, "manifest" ) };

	if ( !JS_IsObject( manifest ) )
	{
		JS_FreeValue( context, manifest );
		return flags;
	}

	JSValue declared { JS_GetPropertyStr( context, manifest, "flags" ) };
	JSValue length_value { JS_GetPropertyStr( context, declared, "length" ) };
	std::uint32_t length {};

	if ( JS_ToUint32( context, &length, length_value ) != 0 ) length = 0;

	JS_FreeValue( context, length_value );

	for ( std::uint32_t index {}; index < length; ++index )
	{
		JSValue entry { JS_GetPropertyUint32( context, declared, index ) };
		JSValue name_value { JS_GetPropertyStr( context, entry, "name" ) };

		if ( const char* name { JS_ToCString( context, name_value ) }; name != nullptr )
		{
			JSValue default_value { JS_GetPropertyStr( context, entry, "default" ) };
			bool enabled { JS_ToBool( context, default_value ) == 1 };
			JS_FreeValue( context, default_value );

			if ( const auto override_value { m_environment.config.flags.find( name ) };
			     override_value != m_environment.config.flags.end() )
				enabled = override_value->second;

			JS_SetPropertyStr( context, flags, name, JS_NewBool( context, enabled ) );
			JS_FreeCString( context, name );
		}

		JS_FreeValue( context, name_value );
		JS_FreeValue( context, entry );
	}

	JS_FreeValue( context, declared );
	JS_FreeValue( context, manifest );
	return flags;
}

void SessionContext::Impl::loadManifestCookies( ScriptExecution& execution )
{
	JSContext* context { execution.context };
	JSValue manifest { JS_GetPropertyStr( context, execution.module_namespace, "manifest" ) };

	if ( !JS_IsObject( manifest ) )
	{
		JS_FreeValue( context, manifest );
		return;
	}

	JSValue cookies { JS_GetPropertyStr( context, manifest, "cookies" ) };
	JS_FreeValue( context, manifest );

	if ( !JS_IsArray( context, cookies ) )
	{
		JS_FreeValue( context, cookies );
		return;
	}

	JSValue length_value { JS_GetPropertyStr( context, cookies, "length" ) };
	std::uint32_t length {};

	if ( JS_ToUint32( context, &length, length_value ) != 0 ) length = 0;

	JS_FreeValue( context, length_value );

	for ( std::uint32_t index {}; index < length; ++index )
	{
		JSValue entry { JS_GetPropertyUint32( context, cookies, index ) };

		if ( !JS_IsObject( entry ) )
		{
			JS_FreeValue( context, entry );
			continue;
		}

		const auto text = [ & ]( const char* property ) -> std::optional< std::string >
		{
			JSValue value { JS_GetPropertyStr( context, entry, property ) };

			if ( !JS_IsString( value ) )
			{
				JS_FreeValue( context, value );
				return std::nullopt;
			}

			std::string output { scriptValueString( context, value ) };
			JS_FreeValue( context, value );
			return output;
		};

		const auto name { text( "name" ) };
		const auto value { text( "value" ) };
		const auto domain { text( "domain" ) };
		const auto path { text( "path" ) };

		JSValue secure_value { JS_GetPropertyStr( context, entry, "secure" ) };
		const bool secure { JS_ToBool( context, secure_value ) == 1 };
		JS_FreeValue( context, secure_value );
		JS_FreeValue( context, entry );

		if ( !name || !value || !domain || name->empty() || domain->empty() )
		{
			spdlog::warn(
				"downloader session: {} declares a manifest cookie without a name, value and domain",
				execution.route.script.filename().string() );
			continue;
		}

		std::string host { *domain };

		while ( host.starts_with( '.' ) ) host.erase( host.begin() );

		m_overlay.set(
			Cookie { .name = *name,
		             .value = *value,
		             .domain = detail::normalizeHost( std::move( host ) ),
		             .path = path.value_or( "/" ),
		             .secure = secure,
		             .host_only = false } );
	}

	JS_FreeValue( context, cookies );
}

SessionContext::SessionContext( std::unique_ptr< Impl > impl ) : m_impl( std::move( impl ) )
{}

SessionContext::~SessionContext() = default;

std::expected< WorkID, std::string > SessionContext::submit(
	std::string url,
	std::optional< WorkID > parent,
	std::function< void( WorkID ) > on_reserved )
{
	return m_impl->submit( std::move( url ), parent, std::move( on_reserved ) );
}

void SessionContext::cancel()
{
	m_impl->cancel();
}

void SessionContext::wait()
{
	m_impl->wait();
}

void SessionContext::close()
{
	m_impl->close();
}

bool SessionContext::idle() const
{
	return m_impl->idle();
}

std::size_t SessionContext::outstanding() const
{
	return m_impl->outstanding();
}

const std::string& SessionContext::rootUrl() const
{
	return m_impl->rootUrl();
}

SessionSnapshot SessionContext::snapshot( const std::uint64_t since ) const
{
	return m_impl->snapshot( since );
}

} // namespace idhan::downloader
