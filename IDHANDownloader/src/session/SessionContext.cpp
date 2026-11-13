#include "session/SessionContext.hpp"

#include <json/writer.h>
#include <spdlog/spdlog.h>

#include <ada.h>
#include <algorithm>
#include <stdexcept>
#include <utility>

#include "HostCallback.hpp"
#include "urls/URLUtils.hpp"
#include "imports/UrlStorageImportSink.hpp"
#include "http/HttpMessage.hpp"
#include "js/JSValueHandle.hpp"
#include "js/ScriptContext.hpp"
#include "js/ScriptTiming.hpp"
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
		m_options.inflight_requests = std::max< std::size_t >( 1, m_environment.config.session_inflight_requests );
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

	std::expected< std::optional< ScriptRoute >, std::string > route_e;
	if ( !parent.has_value() && m_options.generator.isObject() )
	{
		auto generator_e { m_environment.registry->generatorRoute( m_options.generator ) };
		if ( !generator_e.has_value() ) return std::unexpected( generator_e.error() );

		route_e = std::optional< ScriptRoute > { std::move( generator_e.value() ) };
	}
	else
		route_e = m_environment.registry->route( url );
	if ( !route_e.has_value() ) return std::unexpected( std::move( route_e.error() ) );


	auto& route { route_e.value() };
	if ( !route.has_value() ) return std::unexpected( format_ns::format( "No URL class accepts URL: {}", url ) );


	const WorkID id { m_next_work.fetch_add( 1 ) };
	if ( on_reserved ) on_reserved( id );

	if ( !enqueue( id, parent, std::move( url ), std::move( route.value() ) ) )
		return std::unexpected( "The download session is closed" );


	return id;
}

void mergeFollowTags( Json::Value& tags, const Json::Value& inherited )
{
	if ( !inherited.isObject() ) return;

	if ( !tags.isObject() ) tags = Json::Value { Json::objectValue };

	for ( const auto& domain : inherited.getMemberNames() )
	{
		auto& target { tags[ domain ] };
		if ( !target.isArray() ) target = Json::Value { Json::arrayValue };

		for ( const auto& tag : inherited[ domain ] )
		{
			if ( std::ranges::find( target, tag ) == target.end() ) target.append( tag );
		}
	}
}

bool SessionContext::Impl::enqueue(
	const WorkID id,
	const std::optional< WorkID > parent,
	std::string url,
	ScriptRoute route,
	Json::Value tags )
{
	auto handle { m_self.lock() };

	if ( !handle || m_environment.runner == nullptr ) return false;

	{
		const std::scoped_lock lock { m_mutex };
		m_seen.emplace( comparableUrl( url ) );
		++m_queued;
		m_outstanding.fetch_add( 1 );
		m_drained_reported = false;
	}

	// Avoid lock inversion with the runner queue.
	const bool queued { m_environment.runner->submit(
		ScriptWork {
			.handle = std::move( handle ),
			.id = id,
			.parent = parent,
			.url = std::move( url ),
			.tags = std::move( tags ),
			.route = std::move( route ) } ) };

	if ( !queued )
	{
		retireQueued( id );
		return false;
	}

	return true;
}

void SessionContext::Impl::cancel()
{
	{
		const std::scoped_lock lock { m_mutex };
		m_cancelled.store( true );
	}
	notifyWaiters();
	m_environment.lanes->cancel( m_transfer_cancelled );

	if ( const auto handle { m_self.lock() }; handle && m_environment.runner != nullptr )
		m_environment.runner->discard( handle.get() );

	if ( m_environment.runner != nullptr ) m_environment.runner->notifySessionReady( this );
}

void SessionContext::Impl::close()
{
	{
		const std::scoped_lock lock { m_mutex };
		if ( m_closed.exchange( true ) ) return;
	}

	if ( const auto handle { m_self.lock() }; handle && m_environment.runner != nullptr )
		m_environment.runner->discard( handle.get() );

	if ( m_environment.runner != nullptr ) m_environment.runner->notifySessionReady( this );

	notifyWaiters();
}

void SessionContext::Impl::shutdown()
{
	m_shutting_down.store( true );
	close();
}

void SessionContext::Impl::notifyWaiters()
{
	const auto previous { m_changed.exchange( std::make_shared< folly::coro::Baton >() ) };
	previous->post();
}

folly::coro::Task< void > SessionContext::Impl::wait()
{
	for ( ;; )
	{
		const auto changed { m_changed.load() };
		{
			const std::scoped_lock lock { m_mutex };
			if ( m_closed.load() || isDrainedLocked() ) co_return;
		}

		co_await *changed;
	}
}

folly::coro::Task< void > SessionContext::Impl::drain()
{
	for ( ;; )
	{
		const auto changed { m_changed.load() };
		{
			const std::scoped_lock lock { m_mutex };
			if ( isDrainedLocked() ) co_return;
		}

		co_await *changed;
	}
}

bool SessionContext::Impl::isIdleLocked() const
{
	if ( m_outstanding.load() != 0 || !m_pending.empty() || m_import_tasks != 0 ) return false;

	// Completion observers see the idle state they are announcing, while drain() still owns their lifetime.
	return m_drained_reported && m_host_tasks == static_cast< std::size_t >( m_finish_notification );
}

bool SessionContext::Impl::isDrainedLocked() const
{
	return isIdleLocked() && m_host_tasks == 0;
}

bool SessionContext::Impl::idle() const
{
	const std::scoped_lock lock { m_mutex };
	return isIdleLocked();
}

std::size_t SessionContext::Impl::outstanding() const
{
	return m_outstanding.load();
}

bool SessionContext::Impl::reserveStart()
{
	if ( m_cancelled.load() || m_closed.load() ) return false;

	const std::scoped_lock lock { m_mutex };

	// Imports reserve their pending request before preparation; do not count their background task twice.
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
	{
		const std::scoped_lock lock { m_mutex };
		if ( m_starting > 0 ) --m_starting;
	}
	if ( m_environment.runner != nullptr ) m_environment.runner->notifySessionReady( this );
}

void SessionContext::Impl::releaseStartIfReady( ScriptExecution& execution )
{
	if ( !execution.start_reserved ) return;

	// Module evaluation and configure() can yield before the first request is tracked.
	if ( execution.outstanding == 0 && execution.stage != ScriptStage::DONE ) return;

	execution.start_reserved = false;
	releaseStart();
}

void SessionContext::Impl::retire( const WorkID id )
{
	{
		const std::scoped_lock lock { m_mutex };
		m_records.erase( id );
		m_outstanding.fetch_sub( 1 );
	}

	reportDrainedIfIdle();
}

void SessionContext::Impl::reportDrainedIfIdle()
{
	{
		const std::scoped_lock lock { m_mutex };
		if ( m_outstanding.load() != 0 || !m_pending.empty() || m_import_tasks != 0 || m_host_tasks != 0 ) return;

		if ( m_drained_reported ) return;

		m_drained_reported = true;
		m_diagnostics.recordFinished();
		if ( m_options.observer != nullptr )
		{
			m_finish_notification = true;
			const auto queued_e { postHostTaskLocked( session_lane, notifyFinished() ) };
			if ( !queued_e.has_value() )
			{
				m_finish_notification = false;
				spdlog::error( "Unable to queue session completion: {}", queued_e.error() );
			}
		}
	}
	notifyWaiters();
}

SessionContext::Impl::HostTask SessionContext::Impl::notifyFinished()
{
	const auto completed_e { co_await hostCallback( m_options.observer->onFinished() ) };
	if ( !completed_e.has_value() ) spdlog::error( "Session completion observer failed: {}", completed_e.error() );

	const std::scoped_lock lock { m_mutex };
	m_finish_notification = false;
}

void SessionContext::Impl::retireQueued( const WorkID id )
{
	{
		const std::scoped_lock lock { m_mutex };

		if ( m_queued > 0 ) --m_queued;
	}

	retire( id );
}

ParserActivity activityFor( const WorkSnapshot& work, const bool awaiting_result, const bool completion_ready )
{
	if ( work.phase == WorkPhase::TRANSFERRING ) return ParserActivity::FINISHING;

	if ( completion_ready ) return ParserActivity::READY;

	if ( !awaiting_result ) return ParserActivity::READY;

	if ( work.pending_requests > 0 ) return ParserActivity::WAITING_REQUEST;

	if ( work.pending_imports > 0 ) return ParserActivity::WAITING_IMPORT;

	if ( work.pending_host > 0 ) return ParserActivity::WAITING_HOST;

	return ParserActivity::WAITING_PROMISE;
}

SessionSnapshot SessionContext::Impl::snapshot( const std::uint64_t since ) const
{
	SessionSnapshot snapshot { m_diagnostics.snapshot( since ) };
	const auto lanes { m_environment.lanes->snapshots() };
	snapshot.download_bytes_per_second = m_network->speed();
	snapshot.downloaded_bytes = m_network->total();
	if ( snapshot.counters.imported > 0 )
		snapshot.average_import_bytes = snapshot.counters.import_bytes / snapshot.counters.imported;

	{
		const std::scoped_lock lock { m_mutex };
		snapshot.queued = m_queued;
		snapshot.waiting_parsers = m_queued;
		snapshot.running = m_records.size();
		snapshot.in_flight_requests = m_inflight_requests;
		snapshot.outstanding = m_outstanding.load() + m_import_tasks + m_host_tasks
		                     - static_cast< std::size_t >( m_finish_notification );
		snapshot.idle = isIdleLocked();
		snapshot.work.reserve( m_records.size() );
		snapshot.requests.reserve( m_pending.size() );
		snapshot.imports_queued = m_import_queue.size();
		snapshot.imports_active = m_import_tasks - std::min( m_import_tasks, m_import_queue.size() );
		std::size_t host_queued {};
		for ( const auto& [ owner, lane ] : m_host_lanes ) host_queued += lane.size();
		snapshot.host_queued = host_queued;
		snapshot.host_active = m_host_tasks - std::min( m_host_tasks, host_queued );
		std::unordered_map< WorkID, std::size_t > indices;
		std::unordered_set< WorkID > ready;

		for ( const auto& [ id, record ] : m_records )
		{
			if ( record.phase == WorkPhase::MODULE ) ++snapshot.waiting_parsers;

			indices.emplace( id, snapshot.work.size() );
			snapshot.work.push_back(
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
			if ( !pending.ready && !pending.host && !pending.import ) ++snapshot.waiting_requests;

			if ( !pending.ready && !pending.host && !pending.lane.empty() )
			{
				const auto phase { pending.progress->phase.load() };
				if ( phase == NetworkActivity::Phase::DOWNLOADING ) ++snapshot.downloading;

				if ( phase == NetworkActivity::Phase::WAITING || phase == NetworkActivity::Phase::STORAGE )
				{
					++snapshot.waiting;
					if ( phase == NetworkActivity::Phase::STORAGE )
					{
						++snapshot.waiting_storage;
					}
					else
					{
						const auto lane { std::ranges::find( lanes, pending.lane, &LaneSnapshot::key ) };
						if ( lane == lanes.end() )
							++snapshot.waiting_http_shard;
						else if ( lane->dispatch_wait == "backoff" || lane->dispatch_wait == "rate_limit" )
							++snapshot.waiting_http_rate;
						else
							++snapshot.waiting_http_shard;
					}
				}
			}

			snapshot.requests.push_back(
				PendingRequestSnapshot {
					.work = pending.work,
					.url = pending.url,
					.lane = pending.lane,
					.import = pending.import,
					.host = pending.host,
					.ready = pending.ready,
					.bytes = pending.progress ? pending.progress->received.load() : 0,
					.total = pending.progress ? pending.progress->expected.load() : 0,
					.timing = timingOf( pending ) } );

			if ( pending.execution == nullptr ) continue;

			const auto found { indices.find( pending.execution->work_id ) };
			if ( found == indices.end() ) continue;

			auto& work { snapshot.work[ found->second ] };
			if ( pending.ready )
				ready.emplace( work.id );
			else if ( pending.host )
				++work.pending_host;
			else if ( pending.import )
				++work.pending_imports;
			else
				++work.pending_requests;
		}

		for ( auto& work : snapshot.work )
		{
			work.activity = activityFor( work, m_records.at( work.id ).awaiting_result, ready.contains( work.id ) );
			switch ( work.activity )
			{
				case ParserActivity::READY:
					++snapshot.parsers.ready;
					break;
				case ParserActivity::WAITING_REQUEST:
					++snapshot.parsers.waiting_requests;
					break;
				case ParserActivity::WAITING_IMPORT:
					++snapshot.parsers.waiting_imports;
					break;
				case ParserActivity::WAITING_HOST:
					++snapshot.parsers.waiting_host;
					break;
				case ParserActivity::WAITING_PROMISE:
					++snapshot.parsers.waiting_promises;
					break;
				case ParserActivity::FINISHING:
					++snapshot.parsers.finishing;
					break;
			}
		}
	}
	std::vector< std::shared_ptr< ScriptActivity > > activities;
	{
		const std::scoped_lock lock { m_mutex };
		for ( const auto& [ id, activity ] : m_script_activities ) activities.push_back( activity );
	}
	for ( const auto& activity : activities )
	{
		const auto script { activity->snapshot() };
		snapshot.script_executing = snapshot.script_executing || script.executing;
		snapshot.script_busy_ns += script.busy_ns;
		snapshot.script_bursts += script.bursts;
	}
	snapshot.script_worker = m_script_worker.load();
	std::ranges::sort( snapshot.work, {}, &WorkSnapshot::id );
	snapshot.root_url = m_options.root_url;
	snapshot.closed = m_closed.load();
	snapshot.cancelled = m_cancelled.load();
	snapshot.in_flight_limit = m_options.inflight_requests;
	return snapshot;
}

void SessionContext::Impl::attachRuntime( const std::size_t runtime, std::shared_ptr< ScriptActivity > activity )
{
	const std::scoped_lock lock { m_mutex };
	m_script_activities.insert_or_assign( runtime, std::move( activity ) );
	m_script_worker.store( runtime );
}

WorkInfo SessionContext::Impl::infoFor( const ScriptExecution& execution )
{
	return WorkInfo {
		.id = execution.work_id,
		.parent = execution.parent_id,
		.url = execution.parser_url,
		.url_class = execution.route.url_class,
		.handler = execution.route.export_name,
		.script = execution.route.script.filename().string()
	};
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
	const std::size_t runtime )
{
	{
		const std::scoped_lock lock { m_mutex };

		if ( m_queued > 0 ) --m_queued;
	}

	auto execution { std::make_unique< ScriptExecution >() };
	execution->session = this;
	execution->runtime = runtime;
	execution->work_id = work.id;
	execution->parent_id = work.parent;
	execution->parser_url = work.url;
	execution->tags = work.tags;

	const WorkInfo info { infoFor( *execution ) };

	const auto abort = [ & ]( const std::string& error, const FailureStage stage )
	{
		if ( m_shutting_down.load() )
		{
			retire( work.id );
			return;
		}

		spdlog::warn( "downloader session: work {} could not start: {}", work.id, error );
		if ( stage == FailureStage::PARSER ) m_environment.lanes->parserFailed( work.url );

		m_diagnostics.recordFailed( info, error );

		notifyObserver(
			work.id,
			[ info, failure = WorkFailure { .message = error, .stage = stage } ]( SessionObserver& observer )
			{ return observer.onWorkFailed( info, failure ); } );

		retire( work.id );
	};

	execution->route = std::move( work.route );

	if ( spdlog::should_log( spdlog::level::debug ) )
	{
		const auto& matched { execution->route };
		Json::StreamWriterBuilder compact {};
		compact[ "indentation" ] = "";
		const std::string captured {
			matched.parameters.isNull() ? "none" : Json::writeString( compact, matched.parameters )
		};

		spdlog::debug(
			"downloader route: work {} {} -> URL class '{}', export '{}', script {}, captured {}",
			work.id,
			work.url,
			matched.url_class,
			matched.export_name,
			matched.script.filename().string(),
			captured );
	}


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

	const WorkInfo started { infoFor( *execution ) };

	m_diagnostics.recordStarted( started );

	notifyObserver( work.id, [ started ]( SessionObserver& observer ) { return observer.onStarted( started ); } );

	if ( execution->route.download )
	{
		Json::Value options { Json::objectValue };
		options[ "request" ][ "url" ] = work.url;
		options[ "requireFile" ] = true;
		startImport( *execution, std::move( options ) );
		execution->stage = ScriptStage::DONE;
		updateWorkState( *execution );
		return execution;
	}

	auto realm { scripts.createRealm( *execution ) };

	if ( !realm )
	{
		abort( realm.error(), FailureStage::PARSER );
		execution.reset();
		return execution;
	}

	execution->context = *realm;

	auto evaluated { scripts.evaluate( *execution, execution->route.script ) };

	if ( !evaluated )
	{
		const std::string error { evaluated.error() };
		abandon( *execution );
		scripts.freeRealm( execution->context );
		abort( error, FailureStage::PARSER );
		execution.reset();
		return execution;
	}

	execution->result = *evaluated;
	execution->stage = ScriptStage::MODULE;
	updateWorkState( *execution );
	return execution;
}

void SessionContext::Impl::updateWorkState( const ScriptExecution& execution )
{
	const bool awaiting_result { execution.stage != ScriptStage::DONE
		                         && outcomeOf( execution.context, execution.result ) == PromiseOutcome::PENDING };
	const std::scoped_lock lock { m_mutex };
	const auto found { m_records.find( execution.work_id ) };
	if ( found == m_records.end() ) return;

	found->second.phase = phaseOf( execution );
	found->second.awaiting_result = awaiting_result;
}

bool SessionContext::Impl::step( ScriptContext& scripts, ScriptExecution& execution )
{
	if ( m_shutting_down.load() )
	{
		abandon( execution );
		return true;
	}

	if ( execution.stage != ScriptStage::DONE )
	{
		const PromiseOutcome outcome { outcomeOf( execution.context, execution.result ) };

		if ( outcome == PromiseOutcome::PENDING )
		{
			return false;
		}

		if ( outcome == PromiseOutcome::REJECTED )
			failWork( execution, rejectionOf( execution.context, execution.result ) );
		else if ( execution.stage == ScriptStage::MODULE )
		{
			callParser( scripts, execution );
			updateWorkState( execution );
			return false;
		}
		else
			execution.stage = ScriptStage::DONE;
	}

	if ( !execution.failed ) finishWork( execution );

	updateWorkState( execution );
	return execution.outstanding == 0;
}

void SessionContext::Impl::callParser( ScriptContext& scripts, ScriptExecution& execution )
{
	JSContext* context { execution.context };
	const ScriptContext::ExecutionScope timing { scripts, execution.route.script.string() };
	JS_FreeValue( context, execution.result );
	execution.result = JS_UNDEFINED;
	execution.stage = ScriptStage::PARSER;
	{
		const std::scoped_lock lock { m_mutex };
		const auto found { m_records.find( execution.work_id ) };
		if ( found != m_records.end() ) found->second.phase = WorkPhase::PARSER;
	}

	execution.module_namespace = JS_GetModuleNamespace( context, execution.module );

	if ( JS_IsException( execution.module_namespace ) )
	{
		execution.module_namespace = JS_UNDEFINED;
		failWork( execution, scriptExceptionString( context ) );
		return;
	}

	JSValueHandle constructor { context, JS_GetPropertyStr( context, execution.module_namespace, "default" ) };
	if ( !JS_IsFunction( context, constructor.get() ) )
	{
		failWork( execution, "Parser module must default-export a parser class" );
		return;
	}

	JSValueHandle parser {
		context,
		JS_GetPropertyStr( context, execution.module_namespace, "run" ) };

	if ( !JS_IsFunction( context, parser.get() ) )
	{
		failWork(
			execution,
			format_ns::format( "{} does not export its run adapter", execution.route.script.filename().string() ) );
		return;
	}

	JSValueHandle input { context, JS_NewObject( context ) };
	JS_SetPropertyStr( context, input.get(), "url", JS_NewString( context, execution.parser_url.c_str() ) );
	{
		JSValueHandle metadata { context, JS_GetPropertyStr( context, constructor.get(), "metadata" ) };

		if ( !JS_IsObject( metadata.get() ) )
		{
			failWork( execution, "Decorated parser has no metadata" );
			return;
		}

		const auto site { ada::parse< ada::url_aggregator >( execution.parser_url ) };

		if ( !site.has_value() )
		{
			failWork( execution, "Invalid parser URL" );
			return;
		}

		std::string domain { site->get_protocol() };
		domain += "//"
		        + ( execution.route.configured_domain.empty() ?
		                std::string { site->get_hostname() } :
		                execution.route.configured_domain );

		if ( !site->get_port().empty() ) domain += ":" + std::string { site->get_port() };

		JS_SetPropertyStr( context, input.get(), "domain", JS_NewString( context, domain.c_str() ) );
		JS_SetPropertyStr(
			context, input.get(), "operation", JS_NewString( context, execution.route.export_name.c_str() ) );
		Json::StreamWriterBuilder writer;
		JSValueHandle parser_id { context, JS_GetPropertyStr( context, metadata.get(), "id" ) };
		const auto id { scriptValueString( context, parser_id.get() ) };
		const auto& configured_options { m_environment.config.options[ id ] };

		if ( !configured_options.isNull() && !configured_options.isObject() )
		{
			failWork( execution, "Parser options must be an object" );
			return;
		}

		const auto options { Json::writeString(
			writer, configured_options.isNull() ? Json::Value { Json::objectValue } : configured_options ) };
		JS_SetPropertyStr(
			context,
			input.get(),
			"options",
			JS_ParseJSON( context, options.data(), options.size(), "<parser options>" ) );

		if ( execution.route.parameters.isObject() )
		{
			const auto params { Json::writeString( writer, execution.route.parameters ) };
			JS_SetPropertyStr(
				context,
				input.get(),
				"params",
				JS_ParseJSON( context, params.data(), params.size(), "<generator parameters>" ) );
		}

		if ( execution.route.discovery_scripts.isArray() )
		{
			const auto discoverers { Json::writeString( writer, execution.route.discovery_scripts ) };
			JS_SetPropertyStr(
				context,
				input.get(),
				"discoveries",
				JS_ParseJSON( context, discoverers.data(), discoverers.size(), "<discovery hooks>" ) );
			const auto all_options { Json::writeString( writer, m_environment.config.options ) };
			JS_SetPropertyStr(
				context,
				input.get(),
				"parserOptions",
				JS_ParseJSON( context, all_options.data(), all_options.size(), "<parser options>" ) );
		}
	}

	JSValueHandle global { context, JS_GetGlobalObject( context ) };
	JSValueHandle idhan { context, JS_GetPropertyStr( context, global.get(), "idhan" ) };
	JSValue arguments[] { input.get(), idhan.get() };

	JSValue result { JS_Call( context, parser.get(), JS_UNDEFINED, 2, arguments ) };

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
	if ( m_shutting_down.load() ) return;

	if ( execution.settled ) return;

	execution.settled = true;
	spdlog::debug( "downloader session: work {} finished {}", execution.work_id, execution.parser_url );

	m_diagnostics.recordCompleted( infoFor( execution ) );

	notifyObserver(
		execution.work_id,
		[ info = infoFor( execution ) ]( SessionObserver& observer ) { return observer.onCompleted( info ); } );
}

void SessionContext::Impl::failWork( ScriptExecution& execution, std::string error )
{
	if ( m_shutting_down.load() ) return;

	if ( execution.failed ) return;

	execution.failed = true;
	execution.settled = true;
	execution.stage = ScriptStage::DONE;
	execution.error = std::move( error );

	const std::string parser_identity { format_ns::format(
		"parser {} operation '{}'", execution.route.script.filename().string(), execution.route.export_name ) };
	const std::string identity {
		execution.route.url_class.empty() ?
			parser_identity :
			format_ns::format( "URL class '{}', {}", execution.route.url_class, parser_identity )
	};

	spdlog::warn(
		"downloader session: work {} on {} failed: {}", execution.work_id, execution.parser_url, execution.error );

	const std::string reported { format_ns::format( "{}:\n{}", identity, execution.error ) };
	m_diagnostics.recordFailed( infoFor( execution ), reported );

	// A script that lets a failed request's rejection escape fails because of that download.
	const bool download_failed {
		!execution.request_failure.empty() && execution.error.contains( execution.request_failure )
	};
	const WorkFailure failure {
		.message = reported, .stage = download_failed ? FailureStage::DOWNLOAD : FailureStage::PARSER
	};
	if ( failure.stage == FailureStage::PARSER ) m_environment.lanes->parserFailed( execution.parser_url );

	notifyObserver(
		execution.work_id,
		[ info = infoFor( execution ), failure ]( SessionObserver& observer )
		{ return observer.onWorkFailed( info, failure ); } );
}

void SessionContext::Impl::releaseWork( ScriptContext& scripts, ScriptExecution& execution )
{
	(void)postHostTask( execution.work_id, abortDirectImports( execution.work_id ) );
	if ( execution.start_reserved )
	{
		execution.start_reserved = false;
		releaseStart();
	}

	if ( execution.context != nullptr )
	{
		JS_FreeValue( execution.context, execution.result );
		JS_FreeValue( execution.context, execution.module_namespace );
		scripts.freeRealm( execution.context );

		execution.context = nullptr;
		scripts.collectGarbage();
	}

	retire( execution.work_id );
}

void SessionContext::Impl::abandon( ScriptExecution& execution )
{
	(void)postHostTask( execution.work_id, abortDirectImports( execution.work_id ) );
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

			if ( entry->second.import && !entry->second.import_completed )
			{
				// Only the runtime lease holder releases QuickJS values; native work survives realm teardown.
				Pending detached;
				detached.promise = entry->second.promise;
				taken.emplace_back( std::move( detached ) );
				entry->second.promise = {};
				entry->second.execution = nullptr;
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

SessionContext::Impl::HostTask SessionContext::Impl::reportFollowed( WorkInfo info, const FollowStatus status )
{
	m_diagnostics.recordFollowed( info, status );
	if ( m_options.observer == nullptr ) co_return;

	co_await m_options.observer->onFollowed( std::move( info ), status );
}

folly::coro::Task< std::expected< void, std::string > > SessionContext::Impl::followOnHost(
	const WorkID parent,
	std::string url,
	std::optional< std::string > type,
	Json::Value tags )
{
	if ( type.has_value() )
	{
		const auto saved_e { co_await setUrlTypeOnHost( url, type.value() ) };
		if ( !saved_e.has_value() ) co_return std::unexpected( saved_e.error() );
	}

	WorkInfo target { .id = 0, .parent = parent, .url = comparableUrl( url ) };

	if ( m_cancelled.load() || m_closed.load() )
	{
		if ( m_shutting_down.load() )
		{
			// The parser may already be complete; retain this child for startup recovery.
			target.id = m_next_work.fetch_add( 1 );
			co_await reportFollowed( target, FollowStatus::QUEUED );
			co_return std::expected< void, std::string > {};
		}

		co_await reportFollowed( target, FollowStatus::FILTERED );
		co_return std::expected< void, std::string > {};
	}

	// Report outside the lock because observers may request a snapshot.
	bool seen {};

	{
		const std::scoped_lock lock { m_mutex };
		seen = m_seen.contains( comparableUrl( url ) );
	}

	if ( seen )
	{
		co_await reportFollowed( target, FollowStatus::ALREADY_EXPLORED );
		co_return std::expected< void, std::string > {};
	}

	if ( m_options.observer != nullptr && tags.empty() )
	{
		std::optional< RecordID > record { co_await m_options.observer->alreadyImported( url ) };

		if ( record.has_value() )
		{
			{
				const std::scoped_lock lock { m_mutex };
				m_seen.emplace( comparableUrl( url ) );
			}

			co_await reportFollowed( target, FollowStatus::ALREADY_IMPORTED );
			co_return std::expected< void, std::string > {};
		}
	}

	auto route_e { m_environment.registry->route( url, false ) };
	if ( !route_e.has_value() )
	{
		co_await reportFollowed( target, FollowStatus::FILTERED );
		co_return std::expected< void, std::string > {};
	}


	auto& route { route_e.value() };
	if ( !route.has_value() )
	{
		co_await reportFollowed( target, FollowStatus::FILTERED );
		co_return std::expected< void, std::string > {};
	}


	bool claimed {};

	{
		// Insertion atomically claims the URL against concurrent followers.
		const std::scoped_lock lock { m_mutex };
		claimed = m_seen.insert( comparableUrl( url ) ).second;
	}

	if ( !claimed )
	{
		co_await reportFollowed( target, FollowStatus::ALREADY_EXPLORED );
		co_return std::expected< void, std::string > {};
	}

	const WorkID id { m_next_work.fetch_add( 1 ) };
	target.id = id;

	try
	{
		co_await reportFollowed( target, FollowStatus::QUEUED );
	}
	catch ( ... )
	{
		// A failed row insert must not prevent a later follow attempt from retrying.
		const std::scoped_lock lock { m_mutex };
		m_seen.erase( comparableUrl( url ) );
		throw;
	}

	if ( !enqueue( id, parent, std::move( url ), std::move( route.value() ), std::move( tags ) ) )
	{
		if ( m_shutting_down.load() ) co_return std::expected< void, std::string > {};

		const std::string error { "The download session closed before followed work could be queued" };
		m_diagnostics.recordFailed( target, error );

		if ( m_options.observer != nullptr )
			co_await m_options.observer->onWorkFailed(
				target, WorkFailure { .message = error, .stage = FailureStage::CLOSED } );

		co_return std::expected< void, std::string > {};
	}

	co_return std::expected< void, std::string > {};
}

folly::coro::Task< std::expected< void, std::string > > SessionContext::Impl::setUrlTypeOnHost(
	std::string url,
	std::string type )
{
	if ( m_cancelled.load() || m_closed.load() ) co_return std::unexpected( "The download session is closed" );

	auto normalized_e { m_environment.registry->normalizeUrl( url ) };
	if ( !normalized_e.has_value() )
		co_return std::unexpected( "URL type declarations require a valid absolute URL: " + normalized_e.error() );

	if ( m_options.observer == nullptr ) co_return std::expected< void, std::string > {};

	co_return co_await m_options.observer->onUrlTypeSet( std::move( normalized_e.value().url ), std::move( type ) );
}

std::string SessionContext::Impl::comparableUrl( const std::string_view url ) const
{
	auto normalized_e { m_environment.registry->normalizeUrl( url ) };
	if ( !normalized_e.has_value() ) return std::string { url };

	return std::move( normalized_e.value().url );
}

std::expected< Json::Value, std::string > SessionContext::Impl::secrets( const std::string_view domain )
{
	if ( m_environment.secrets == nullptr ) return Json::Value { Json::objectValue };

	return hostOperation( [ & ] { return m_environment.secrets->secrets( domain ); } );
}

std::expected< void, std::string > SessionContext::Impl::setCookie(
	ScriptExecution& execution,
	std::string name,
	std::string value )
{
	const auto site { ada::parse< ada::url_aggregator >( execution.parser_url ) };

	if ( !site.has_value() || name.empty() ) return std::unexpected( "Invalid cookie or parser URL" );

	const auto domain {
		execution.route.configured_domain.empty() ?
			std::string { site->get_hostname() } :
			execution.route.configured_domain
	};
	m_overlay.set(
		Cookie { .name = std::move( name ),
		         .value = std::move( value ),
		         .domain = domain,
		         .path = "/",
		         .secure = site->get_protocol() == "https:",
		         .host_only = false } );
	return {};
}

folly::coro::Task< std::expected< void, std::string > > SessionContext::Impl::learnHostOnHost(
	std::string source,
	std::string origin,
	std::string url_class )
{
	const auto site { ada::parse< ada::url_aggregator >( source ) };
	const auto learned { ada::parse< ada::url_aggregator >( origin ) };

	if ( !site.has_value() || !learned.has_value() ) co_return std::unexpected( "Invalid URL for a learned host" );

	if ( site->get_hostname() != learned->get_hostname() )
		co_return std::unexpected( "Only the host being discovered can be learned" );

	const std::string host { detail::normalizeHost( std::string { learned->get_hostname() } ) };
	auto learned_e { co_await m_environment.registry->learnHost( host, url_class ) };

	if ( !learned_e.has_value() ) co_return learned_e;

	m_environment.lanes->reconfigure( host );
	co_return std::expected< void, std::string > {};
}

std::uint64_t SessionContext::Impl::track( ScriptExecution& execution, Pending pending )
{
	if ( !pending.import ) pending.work = execution.work_id;
	pending.runtime = execution.runtime;
	pending.started_at = std::chrono::steady_clock::now();

	if ( !pending.import || pending.promise.context != nullptr )
	{
		pending.execution = &execution;
		++execution.outstanding;
	}

	const std::scoped_lock lock { m_mutex };
	const std::uint64_t id { m_next_pending++ };
	++m_inflight_requests;

	if ( pending.execution != nullptr )
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

	std::size_t runtime {};
	bool detached {};
	WorkID import_work {};
	std::shared_ptr< NetworkActivity::Request > progress {};

	{
		const std::scoped_lock lock { m_mutex };
		const auto found { m_pending.find( id ) };

		if ( found == m_pending.end() ) return;

		progress = found->second.progress;
		import_work = found->second.work;
		found->second.result = std::move( result );
		found->second.ready = !found->second.import;
		runtime = found->second.runtime;
		detached = found->second.import && m_environment.import_executor != nullptr;
		if ( detached ) ++m_import_tasks;
	}

	if ( progress ) progress->finish();

	if ( detached )
	{
		try
		{
			folly::coro::co_withExecutor( m_environment.import_executor, completeDetachedImport( handle, id ) )
				.start( reportHostTaskResult );
		}
		catch ( const std::exception& error )
		{
			WorkID work {};
			std::string url;
			{
				const std::scoped_lock lock { m_mutex };
				const auto found { m_pending.find( id ) };
				if ( found != m_pending.end() )
				{
					work = found->second.work;
					url = found->second.url;
				}
			}
			reportImportFailure( work, url, error.what() );
			completeImport( id, std::unexpected( std::string { error.what() } ) );
			finishImportTask( false );
		}
		return;
	}

	if ( m_environment.runner->complete( runtime, ScriptCompletion { .handle = std::move( handle ), .pending = id } ) )
		return;

	// Runtime teardown abandons pending requests under its exclusive lease.
	reportDrainedIfIdle();
}

void SessionContext::Impl::completeImport(
	const std::uint64_t id,
	std::expected< void, std::string > result_e,
	const bool existing )
{
	auto handle { m_self.lock() };
	if ( !handle ) return;

	std::optional< std::size_t > runtime;
	{
		const std::scoped_lock lock { m_mutex };
		const auto found { m_pending.find( id ) };
		if ( found == m_pending.end() ) return;

		auto& pending { found->second };
		if ( pending.import_completed ) return;

		pending.import_completed = true;
		pending.existing_import = existing;
		pending.ready = true;
		pending.host_result_e = std::move( result_e );
		if ( pending.import_status != nullptr )
		{
			if ( pending.host_result_e.has_value() )
				pending.import_status->result_e = existing;
			else
				pending.import_status->result_e = std::unexpected( pending.host_result_e.error() );
		}

		if ( pending.promise.context != nullptr )
			runtime = pending.runtime;
		else
		{
			m_pending.erase( found );
			--m_inflight_requests;
		}
	}
	if ( runtime.has_value() )
	{
		// If the runner stops concurrently, abandon() discards the promise under its runtime lease.
		(void)m_environment.runner->complete(
			runtime.value(), ScriptCompletion { .handle = std::move( handle ), .pending = id } );
	}
	m_environment.runner->notifySessionReady( this );
	reportDrainedIfIdle();
}

void SessionContext::Impl::completePendingOperation( const std::uint64_t id )
{
	Pending pending {};

	{
		const std::scoped_lock lock { m_mutex };
		const auto found { m_pending.find( id ) };

		if ( found == m_pending.end() ) return;

		// Import persistence finishes on the import executor before its promise can be resolved here.
		if ( found->second.import && !found->second.import_completed ) return;

		pending = std::move( found->second );
		m_pending.erase( found );

		if ( m_inflight_requests > 0 ) --m_inflight_requests;
	}
	m_environment.runner->notifySessionReady( this );

	if ( pending.execution != nullptr && pending.execution->outstanding > 0 ) --pending.execution->outstanding;

	if ( pending.execution != nullptr )
	{
		const std::scoped_lock lock { m_mutex };

		if ( const auto found { m_records.find( pending.execution->work_id ) }; found != m_records.end() )
			found->second.outstanding = pending.execution->outstanding;
	}

	const ScriptContext::ExecutionScope timing { pending.promise.context };
	if ( pending.host || pending.import )
	{
		if ( pending.host_result_e.has_value() )
		{
			JSValue output { JS_UNDEFINED };
			if ( pending.import )
			{
				output = JS_NewObject( pending.promise.context );
				JS_SetPropertyStr(
					pending.promise.context,
					output,
					"existing",
					JS_NewBool( pending.promise.context, pending.existing_import ) );
			}

			pending.promise.resolveOrReject( true, output );
		}
		else
			rejectPending( pending, pending.host_result_e.error() );
	}
	else
		completeRequest( pending );

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
			promise.resolveOrReject( false, error );
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
				promise.resolveOrReject( false, error );
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
		promise.resolveOrReject( false, error );
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
			.import = false,
			.allow_http_errors = options[ "allowHttpErrors" ].asBool() } ) };

	spdlog::debug( "downloader session: work {} queuing request for {}", execution.work_id, recorded_url );

	auto handle { m_self.lock() };
	{
		const std::scoped_lock lock { m_mutex };
		const auto found { m_pending.find( id ) };
		if ( found != m_pending.end() ) request->progress = found->second.progress;
	}
	attachTransferListener( request->progress, execution.work_id, false );
	request->network = m_network;
	m_environment.lanes->send(
		std::move( *request ),
		[ handle = std::move( handle ), id ]( TransferResult result )
		{
			if ( handle ) handle->impl().deliver( id, std::move( result ) );
		} );

}

void SessionContext::Impl::attachTransferListener(
	const std::shared_ptr< NetworkActivity::Request >& progress,
	const WorkID work,
	const bool import )
{
	if ( !progress ) return;

	SessionObserver* observer { m_options.observer };
	if ( observer == nullptr ) return;

	progress->listener = [ observer, work, import ]( const TransferEvent event )
	{ observer->onTransfer( work, event, import ); };
}

std::shared_ptr< ImportStatus > SessionContext::Impl::startImport( ScriptExecution& execution, Json::Value options )
{
	auto status { std::make_shared< ImportStatus >() };
	status->session = m_self;
	if ( !acceptsWork() )
	{
		status->result_e = std::unexpected( "The download session is closed" );
		return status;
	}

	mergeFollowTags( options[ "tags" ], execution.tags );

	const auto url { options[ "request" ][ "url" ].asString() };
	const WorkID work { m_next_work.fetch_add( 1 ) };
	const auto id { track( execution, Pending { .work = work, .url = url, .import = true, .import_status = status } ) };
	{
		const std::scoped_lock lock { m_mutex };
		status->pending_id = id;
		++m_import_tasks;
		m_drained_reported = false;
	}
	const auto queued_e { postHostTask(
		execution.work_id,
		stageImport(
			ImportPreparation {
				.work = work,
				.pending = id,
				.parent = execution.work_id,
				.options = std::move( options ) } ) ) };
	if ( !queued_e.has_value() )
	{
		reportImportFailure( work, url, queued_e.error(), execution.work_id );
		completeImport( id, std::unexpected( queued_e.error() ) );
		finishImportTask( false );
	}
	return status;
}

void SessionContext::Impl::awaitImport(
	ScriptExecution& execution,
	std::shared_ptr< ImportStatus > status,
	PendingPromise promise )
{
	if ( status->session.lock() != m_self.lock() )
	{
		promise.resolveOrReject(
			false, JS_NewString( promise.context, "Import belongs to a different download session" ) );
		return;
	}

	std::optional< std::expected< bool, std::string > > result_e;
	{
		const std::scoped_lock lock { m_mutex };
		result_e = status->result_e;
		if ( !result_e.has_value() )
		{
			auto& pending { m_pending.at( status->pending_id ) };
			pending.promise = promise;
			pending.execution = &execution;
			pending.runtime = execution.runtime;
			++execution.outstanding;
			const auto found { m_records.find( execution.work_id ) };
			if ( found != m_records.end() ) found->second.outstanding = execution.outstanding;

			return;
		}
	}
	const auto& outcome_e { result_e.value() };
	if ( !outcome_e.has_value() )
	{
		JSValue error { JS_NewError( promise.context ) };
		JS_SetPropertyStr(
			promise.context, error, "message", JS_NewString( promise.context, outcome_e.error().c_str() ) );
		promise.resolveOrReject( false, error );
		return;
	}

	JSValue output { JS_NewObject( promise.context ) };
	JS_SetPropertyStr( promise.context, output, "existing", JS_NewBool( promise.context, outcome_e.value() ) );
	promise.resolveOrReject( true, output );
}

SessionContext::Impl::HostTask SessionContext::Impl::stageImport( ImportPreparation preparation )
{
	{
		const std::scoped_lock lock { m_mutex };
		m_import_queue.emplace_back( std::move( preparation ) );
	}
	dispatchImports();
	co_return;
}

void SessionContext::Impl::dispatchImports()
{
	for ( ;; )
	{
		ImportPreparation preparation;
		{
			const std::scoped_lock lock { m_mutex };
			if ( m_import_queue.empty() ) return;

			if ( m_import_preparations >= m_options.inflight_requests ) return;

			preparation = std::move( m_import_queue.front() );
			m_import_queue.pop_front();
			++m_import_preparations;
		}
		const auto parent { preparation.parent };
		const auto work { preparation.work };
		const auto id { preparation.pending };
		const auto url { preparation.options[ "request" ][ "url" ].asString() };
		const auto handle { m_self.lock() };
		try
		{
			folly::coro::co_withExecutor(
				m_environment.import_executor, prepareImport( handle, std::move( preparation ) ) )
				.start( reportHostTaskResult );
		}
		catch ( const std::exception& error )
		{
			reportImportFailure( work, url, error.what(), parent );
			completeImport( id, std::unexpected( std::string { error.what() } ) );
			const std::scoped_lock lock { m_mutex };
			--m_import_preparations;
			--m_import_tasks;
		}
	}
}

SessionContext::Impl::HostTask SessionContext::Impl::prepareImport(
	std::shared_ptr< SessionContext > handle,
	ImportPreparation preparation )
{
	auto& session { handle->impl() };
	const auto work { preparation.work };
	const auto id { preparation.pending };
	const auto parent { preparation.parent };
	const auto url { preparation.options[ "request" ][ "url" ].asString() };
	std::expected< void, std::string > prepared_e { std::unexpected( "Import cancelled" ) };
	if ( !session.m_cancelled.load() )
		prepared_e = co_await hostOperation( session.startImportImpl( std::move( preparation ) ) );

	if ( !prepared_e.has_value() )
	{
		session.reportImportFailure( work, url, prepared_e.error(), parent );
		session.completeImport( id, std::unexpected( prepared_e.error() ) );
	}
	session.finishImportTask();
}

void SessionContext::Impl::finishImportTask( const bool preparation )
{
	{
		const std::scoped_lock lock { m_mutex };
		--m_import_tasks;
		if ( preparation ) --m_import_preparations;
	}
	if ( preparation ) dispatchImports();

	notifyWaiters();
	m_environment.runner->notifySessionReady( this );
	reportDrainedIfIdle();
}

//! Storage failures happen after the file arrived; anything else means the file never did.
static FailureStage importFailureStage( const TransferResult& result )
{
	if ( !result )
		return result.error().code == TransferErrorCode::SINK ? FailureStage::IMPORT : FailureStage::DOWNLOAD;

	if ( !result->import.has_value() ) return FailureStage::DOWNLOAD;

	return FailureStage::IMPORT;
}

SessionContext::Impl::HostTask SessionContext::Impl::completeDetachedImport(
	std::shared_ptr< SessionContext > handle,
	const std::uint64_t id )
{
	auto& session { handle->impl() };
	std::optional< Pending > native;
	{
		const std::scoped_lock lock { session.m_mutex };
		const auto found { session.m_pending.find( id ) };
		if ( found != session.m_pending.end() )
		{
			auto& pending { found->second };
			native.emplace(
				Pending {
					.progress = pending.progress,
					.work = pending.work,
					.url = pending.url,
					.lane = pending.lane,
					.sensitive_query = pending.sensitive_query,
					.import_options = std::move( pending.import_options ),
					.import = true,
					.result = std::move( pending.result ),
					.started_at = pending.started_at } );
		}
	}
	if ( native.has_value() )
	{
		const auto work { native->work };
		const auto url { native->url };
		const auto stage { importFailureStage( native->result ) };
		const bool existing { native->result.has_value() && native->result.value().import.has_value()
			                  && native->result.value().import.value().existing };
		auto settled_e { co_await hostOperation( session.settleImport( std::move( native.value() ) ) ) };
		if ( !settled_e.has_value() ) session.reportImportFailure( work, url, settled_e.error(), std::nullopt, stage );

		session.completeImport( id, std::move( settled_e ), existing );
	}
	session.finishImportTask( false );
}

folly::coro::Task< std::expected< void, std::string > > SessionContext::Impl::startImportImpl(
	ImportPreparation preparation )
{
	const WorkID import_id { preparation.work };
	const auto id { preparation.pending };
	const auto parent { preparation.parent };
	auto& options { preparation.options };
	std::string response_type {};
	auto request { buildRequest( options[ "request" ], response_type ) };

	if ( !request )
	{
		co_return std::unexpected( request.error() );
	}

	if ( m_environment.imports == nullptr )
	{
		co_return std::unexpected( std::string { "This downloader host cannot import files" } );
	}

	auto import_url_e { m_environment.registry->normalizeUrl( request->url ) };
	if ( !import_url_e.has_value() ) co_return std::unexpected( import_url_e.error() );

	auto import_options_e { m_environment.registry->normalizeOptionUrls( options ) };
	if ( !import_options_e.has_value() ) co_return std::unexpected( import_options_e.error() );

	auto& import_options { import_options_e.value() };
	const WorkInfo info { .id = import_id, .parent = parent, .url = import_url_e.value().url };

	if ( m_options.observer != nullptr )
	{
		const auto queued_e { co_await hostOperation( m_options.observer->onImportQueued( info ) ) };

		if ( !queued_e.has_value() )
		{
			co_return std::unexpected( queued_e.error() );
		}
	}

	const auto& urls { std::as_const( import_options )[ "urls" ] };
	std::vector< std::pair< std::string, std::string > > types;
	if ( urls.isArray() )
	{
		for ( const auto& entry : urls )
		{
			if ( !entry.isObject() || !entry[ "type" ].isString() ) continue;

			types.emplace_back( entry[ "url" ].asString(), entry[ "type" ].asString() );
		}
	}
	if ( m_options.observer != nullptr && !types.empty() )
	{
		const auto saved_e {
			co_await hostOperation( m_options.observer->onUrlTypesSet( std::move( types ) ) )
		};
		if ( !saved_e.has_value() )
		{
			co_return std::unexpected( saved_e.error() );
		}
	}

	import_options.removeMember( "request" );

	ImportRequest import {
		.work = import_id,
		.url = std::move( import_url_e.value().url ),
		.options = import_options,
		.session_id = m_options.session_id
	};

	const auto& force_download { options[ "forceDownload" ] };
	if ( !force_download.isNull() && !force_download.isBool() )
	{
		co_return std::unexpected( std::string { "forceDownload must be a boolean" } );
	}
	import.force_download = m_options.force_download || m_environment.config.force_download || force_download.asBool();

	if ( !import.force_download )
	{
		auto stored_e { co_await hostOperation( m_environment.imports->findStoredRecord( import ) ) };
		if ( !stored_e.has_value() ) co_return std::unexpected( stored_e.error() );


		import.stored_record = stored_e.value();
	}

	auto sink_e { hostOperation( [ & ] { return m_environment.imports->open( import ); } ) };
	if ( !sink_e.has_value() ) co_return std::unexpected( sink_e.error() );


	auto& sink { sink_e.value() };
	// Complete local reuse before LanePool::send: no HTTP request means no rate-limit claim.
	if ( import.stored_record.has_value() )
	{
		const auto imported_e { co_await hostOperation( sink->finish( {} ) ) };
		if ( !imported_e.has_value() )
		{
			(void)hostCallback( [ & ] { sink->abort(); } );
			co_return std::unexpected( imported_e.error() );
		}


		const auto& imported { imported_e.value() };
		const ImportInfo info {
			.work = import_id, .url = import.url, .record_id = imported.record_id,
			.note = imported.note,
			.existing = imported.existing,
			.posted_at = options[ "postedAt" ].isInt64() ?
			                 std::optional< std::int64_t > { options[ "postedAt" ].asInt64() } :
			                 std::nullopt
		};
		m_diagnostics.recordImported( info );
		if ( m_options.observer != nullptr )
		{
			co_await m_options.observer->onImported( info );
		}

		completeImport( id, {}, imported.existing );
		co_return std::expected< void, std::string > {};
	}


	request->sink = std::make_unique< UrlStorageImportSink >( std::move( sink ), *m_environment.registry );
	request->import = std::move( import );
	request->cancellation = m_transfer_cancelled;

	const std::string url { request->url };

	const auto lane { m_environment.lanes->laneKeyForUrl( url ) };
	{
		const std::scoped_lock lock { m_mutex };
		const auto found { m_pending.find( id ) };
		if ( found == m_pending.end() ) co_return std::unexpected( "The import is no longer tracked" );

		found->second.url = url;
		found->second.lane = lane;
		found->second.import_options = std::move( import_options );
	}

	spdlog::debug( "downloader session: work {} queued import {}", parent, url );

	auto handle { m_self.lock() };
	{
		const std::scoped_lock lock { m_mutex };
		const auto found { m_pending.find( id ) };
		if ( found != m_pending.end() ) request->progress = found->second.progress;
	}
	attachTransferListener( request->progress, import_id, true );
	request->network = m_network;
	m_environment.lanes->send(
		std::move( *request ),
		[ handle = std::move( handle ), id ]( TransferResult result )
		{
			if ( handle ) handle->impl().deliver( id, std::move( result ) );
		} );

	co_return std::expected< void, std::string > {};
}

void SessionContext::Impl::rejectPending( Pending& pending, const std::string& error )
{
	JSContext* context { pending.promise.context };

	if ( context == nullptr ) return;

	const ScriptContext::ExecutionScope timing { context };
	JSValue value { JS_NewError( context ) };
	JS_SetPropertyStr( context, value, "message", JS_NewString( context, error.c_str() ) );
	pending.promise.resolveOrReject( false, value );
}

void SessionContext::Impl::reportImportFailure(
	const WorkID work,
	const std::string& url,
	const std::string& error,
	const std::optional< WorkID > parent,
	const FailureStage stage )
{
	if ( m_shutting_down.load() ) return;

	spdlog::warn( "downloader session: work {} import of {} failed: {}", work, url, error );
	m_diagnostics.recordImportFailed( work, url, error );

	notifyObserver(
		parent.value_or( work ),
		[ info = WorkInfo { .id = work, .parent = parent, .url = url },
		  url,
		  failure = WorkFailure { .message = error, .stage = stage } ]( SessionObserver& observer )
		{ return observer.onImportFailure( info, url, failure ); } );
}

RequestTiming SessionContext::Impl::timingOf( const Pending& pending )
{
	if ( !pending.progress ) return RequestTiming { .made = pending.started_at };

	return pending.progress->timing( pending.started_at );
}

void SessionContext::Impl::reportRequestFailure( const RequestFailure& failure )
{
	if ( m_shutting_down.load() ) return;

	spdlog::warn(
		"downloader session: work {} request to {} answered {}: {} ({})",
		failure.work,
		failure.url,
		failure.status.has_value() ? std::to_string( *failure.status ) : "no response",
		failure.message,
		describeRequestTiming( failure.timing ) );
	m_diagnostics.recordRequestFailed( failure );

	notifyObserver(
		failure.work, [ failure ]( SessionObserver& observer ) { return observer.onRequestFailed( failure ); } );
}

void SessionContext::Impl::completeRequest( Pending& pending )
{
	JSContext* context { pending.promise.context };

	if ( context == nullptr ) return;

	TransferResult& result { pending.result };
	const RequestTiming timing { timingOf( pending ) };

	if ( !result )
	{
		reportRequestFailure(
			RequestFailure {
				.work = pending.work,
				.url = pending.url,
				.lane = pending.lane,
				.status = std::nullopt,
				.message = result.error().message,
				.timing = timing } );
		const std::string rejection { format_ns::format( "{}: {}", pending.url, result.error().message ) };
		if ( pending.execution != nullptr ) pending.execution->request_failure = rejection;

		rejectPending( pending, rejection );
		return;
	}

	TransferResponse& response { *result };

	const RequestInfo request_info {
		.work = pending.work,
		.url = detail::redactUrlQuery( response.url, pending.sensitive_query ),
		.lane = pending.lane,
		.status = response.status,
		.bytes = response.bytes,
		.timing = timing
	};
	m_diagnostics.recordRequest( request_info );
	spdlog::debug(
		"downloader session: work {} request to {} answered {} ({})",
		request_info.work,
		request_info.url,
		request_info.status,
		describeRequestTiming( timing ) );

	notifyObserver(
		request_info.work,
		[ request_info ]( SessionObserver& observer ) { return observer.onRequest( request_info ); } );

	if ( response.status != 200 )
		reportRequestFailure(
			RequestFailure {
				.work = pending.work,
				.url = request_info.url,
				.lane = pending.lane,
				.status = response.status,
				.message = describeBody( response.body, response.bytes ),
				.timing = timing } );

	if ( response.status == 404 && !pending.allow_http_errors )
	{
		const std::string rejection { format_ns::format( "{} returned HTTP 404 (Not Found)", request_info.url ) };
		if ( pending.execution != nullptr ) pending.execution->request_failure = rejection;

		rejectPending( pending, rejection );
		return;
	}

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

	pending.promise.resolveOrReject( true, output );
}

folly::coro::Task< std::expected< void, std::string > > SessionContext::Impl::settleImport( Pending pending )
{
	TransferResult& result { pending.result };
	const RequestTiming timing { timingOf( pending ) };

	if ( !result )
	{
		reportRequestFailure(
			RequestFailure {
				.work = pending.work,
				.url = pending.url,
				.lane = pending.lane,
				.status = std::nullopt,
				.message = result.error().message,
				.timing = timing } );
		co_return std::unexpected( format_ns::format( "{}: {}", pending.url, result.error().message ) );
	}

	TransferResponse& response { *result };

	if ( response.status != 200 )
		reportRequestFailure(
			RequestFailure {
				.work = pending.work,
				.url = detail::redactUrlQuery( response.url, pending.sensitive_query ),
				.lane = pending.lane,
				.status = response.status,
				.message = describeBody( response.body, response.bytes ),
				.timing = timing } );

	if ( !response.import.has_value() )
	{
		if ( response.status == 404 )
			co_return std::unexpected( format_ns::format( "{} returned HTTP 404 (Not Found)", pending.url ) );

		co_return std::unexpected(
			format_ns::format( "{} returned HTTP {} instead of a file", pending.url, response.status ) );
	}

	const ImportMetadata& metadata { response.import_metadata };

	const ImportInfo import_info {
		.work = pending.work,
		.url = response.url,
		.record_id = response.import->record_id,
		.size = metadata.size,
		.content_type = metadata.content_type,
		.filename = metadata.filename,
		.note = response.import->note,
		.existing = response.import->existing,
		.timing = timing,
		.posted_at = pending.import_options[ "postedAt" ].isInt64() ?
		                 std::optional< std::int64_t > { pending.import_options[ "postedAt" ].asInt64() } :
		                 std::nullopt
	};
	m_diagnostics.recordImported( import_info );
	spdlog::debug(
		"downloader session: work {} import of {} received ({})",
		import_info.work,
		pending.url,
		describeRequestTiming( timing ) );

	if ( m_options.observer != nullptr )
	{
		co_await m_options.observer->onImported( import_info );
	}

	co_return std::expected< void, std::string > {};
}

std::expected< TransferRequest, std::string > SessionContext::Impl::buildRequest(
	const Json::Value& options,
	std::string& response_type )
{
	TransferRequest request {};
	request.url = options[ "url" ].isString() ? options[ "url" ].asString() : std::string {};

	if ( request.url.empty() ) return std::unexpected( "A request needs a url" );

	const auto& allow_http_errors { options[ "allowHttpErrors" ] };
	if ( !allow_http_errors.isNull() && !allow_http_errors.isBool() )
		return std::unexpected( "request allowHttpErrors must be a boolean" );

	request.options.stop_on_not_found = !allow_http_errors.asBool();

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

std::uint64_t SessionContext::downloadedBytes() const
{
	return m_impl->downloadedBytes();
}

folly::coro::Task< void > SessionContext::wait()
{
	co_await m_impl->wait();
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
