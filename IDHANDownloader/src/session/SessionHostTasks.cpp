#include <folly/ScopeGuard.h>
#include <folly/coro/Sleep.h>
#include <folly/futures/ThreadWheelTimekeeper.h>

#include <ada.h>

#include "HostCallback.hpp"
#include "session/SessionContext.hpp"

namespace idhan::downloader
{

std::expected< void, std::string > SessionContext::Impl::postHostTaskLocked( const WorkID owner, HostTask task )
{
	const auto handle { m_self.lock() };
	if ( !handle || m_environment.import_executor == nullptr )
		return std::unexpected( "The session has no host executor" );

	const auto [ lane, created ] { m_host_lanes.try_emplace( owner ) };
	lane->second.emplace_back( std::move( task ) );
	++m_host_tasks;
	if ( !created ) return {};

	try
	{
		folly::coro::co_withExecutor( m_environment.import_executor, runHostLane( handle, owner ) )
			.start( reportHostTaskResult );
	}
	catch ( const std::exception& error )
	{
		m_host_lanes.erase( lane );
		--m_host_tasks;
		return std::unexpected( std::string { error.what() } );
	}
	return {};
}

std::expected< void, std::string > SessionContext::Impl::postHostTask( const WorkID owner, HostTask task )
{
	const std::scoped_lock lock { m_mutex };
	return postHostTaskLocked( owner, std::move( task ) );
}

std::expected< void, std::string > SessionContext::Impl::spawnHostTask( HostTask task )
{
	const auto handle { m_self.lock() };
	if ( !handle || m_environment.import_executor == nullptr )
		return std::unexpected( "The session has no host executor" );

	{
		const std::scoped_lock lock { m_mutex };
		++m_host_tasks;
	}
	try
	{
		folly::coro::co_withExecutor( m_environment.import_executor, runDetachedHostTask( handle, std::move( task ) ) )
			.start( reportHostTaskResult );
	}
	catch ( const std::exception& error )
	{
		const std::scoped_lock lock { m_mutex };
		--m_host_tasks;
		return std::unexpected( std::string { error.what() } );
	}
	return {};
}

SessionContext::Impl::HostTask SessionContext::Impl::drainHostLane( const WorkID owner )
{
	for ( ;; )
	{
		std::optional< HostTask > task;
		{
			const std::scoped_lock lock { m_mutex };
			const auto lane { m_host_lanes.find( owner ) };
			if ( lane->second.empty() )
			{
				m_host_lanes.erase( lane );
				co_return;
			}

			task.emplace( std::move( lane->second.front() ) );
			lane->second.pop_front();
		}
		co_await runHostTask( std::move( task.value() ) );
	}
}

SessionContext::Impl::HostTask SessionContext::Impl::runHostTask( HostTask task )
{
	std::expected< void, std::string > completed_e { co_await hostCallback( std::move( task ) ) };

	if ( !completed_e.has_value() ) spdlog::error( "Downloader host callback failed: {}", completed_e.error() );

	{
		const std::scoped_lock lock { m_mutex };
		--m_host_tasks;
	}
	reportDrainedIfIdle();
	notifyWaiters();
	m_environment.runner->notifySessionReady( this );
}

SessionContext::Impl::HostTask SessionContext::Impl::runHostLane(
	std::shared_ptr< SessionContext > handle,
	const WorkID owner )
{
	co_await handle->impl().drainHostLane( owner );
}

SessionContext::Impl::HostTask SessionContext::Impl::runDetachedHostTask(
	std::shared_ptr< SessionContext > handle,
	HostTask task )
{
	co_await handle->impl().runHostTask( std::move( task ) );
}

void SessionContext::Impl::notifyObserver( const WorkID owner, ObserverCallback callback )
{
	if ( m_options.observer == nullptr ) return;

	const auto queued_e { postHostTask( owner, runObserver( std::move( callback ) ) ) };
	if ( !queued_e.has_value() ) spdlog::error( "Unable to queue downloader observer: {}", queued_e.error() );
}

SessionContext::Impl::HostTask SessionContext::Impl::runObserver( ObserverCallback callback )
{
	co_await callback( *m_options.observer );
}

SessionContext::Impl::HostTask SessionContext::Impl::runHostOperation(
	const std::uint64_t id,
	folly::coro::Task< std::expected< void, std::string > > operation )
{
	deliverHost( id, co_await hostOperation( std::move( operation ) ) );
}

void SessionContext::Impl::startHostOperation(
	ScriptExecution& execution,
	PendingPromise promise,
	folly::coro::Task< std::expected< void, std::string > > operation,
	const HostOrder order )
{
	if ( !acceptsWork() )
	{
		promise.resolveOrReject( false, JS_NewString( promise.context, "The download session is closed" ) );
		return;
	}

	const auto id { track( execution, Pending { .promise = promise, .url = execution.parser_url, .host = true } ) };
	auto task { runHostOperation( id, std::move( operation ) ) };
	std::expected< void, std::string > queued_e {};
	if ( order == HostOrder::UNORDERED )
		queued_e = spawnHostTask( std::move( task ) );
	else
		queued_e = postHostTask( execution.work_id, std::move( task ) );

	if ( !queued_e.has_value() ) deliverHost( id, std::unexpected( queued_e.error() ) );
}

void SessionContext::Impl::deliverHost( const std::uint64_t id, std::expected< void, std::string > result )
{
	auto handle { m_self.lock() };
	if ( !handle ) return;

	std::size_t runtime {};
	{
		const std::scoped_lock lock { m_mutex };
		const auto found { m_pending.find( id ) };
		if ( found == m_pending.end() ) return;

		found->second.host_result_e = std::move( result );
		found->second.ready = true;
		runtime = found->second.runtime;
	}
	if ( m_environment.runner->complete( runtime, ScriptCompletion { .handle = std::move( handle ), .pending = id } ) )
		return;

	// Runtime teardown discards realm-bound promises after execution stops.
	reportDrainedIfIdle();
}

void SessionContext::Impl::startFollow(
	ScriptExecution& execution,
	std::string url,
	std::optional< std::string > type,
	Json::Value tags,
	PendingPromise promise )
{
	mergeFollowTags( tags, execution.tags );
	startHostOperation(
		execution, promise, followOnHost( execution.work_id, std::move( url ), std::move( type ), std::move( tags ) ) );
}

void SessionContext::Impl::startSetUrlType(
	ScriptExecution& execution,
	std::string url,
	std::string type,
	PendingPromise promise )
{
	startHostOperation( execution, promise, setUrlTypeOnHost( std::move( url ), std::move( type ) ) );
}

void SessionContext::Impl::startLearnHost(
	ScriptExecution& execution,
	std::string origin,
	std::string url_class,
	PendingPromise promise )
{
	startHostOperation(
		execution, promise, learnHostOnHost( execution.parser_url, std::move( origin ), std::move( url_class ) ) );
}

// Shared timer ownership also supports embedding hosts that do not initialize Folly's singleton vault.
folly::ThreadWheelTimekeeper& downloaderTimekeeper()
{
	static folly::ThreadWheelTimekeeper timekeeper;
	return timekeeper;
}

void SessionContext::Impl::startDelay(
	ScriptExecution& execution,
	const std::uint32_t milliseconds,
	PendingPromise promise )
{
	startHostOperation( execution, promise, delayOnHost( milliseconds ), HostOrder::UNORDERED );
}

folly::coro::Task< std::expected< void, std::string > > SessionContext::Impl::delayOnHost(
	const std::uint32_t milliseconds )
{
	const auto deadline { std::chrono::steady_clock::now() + std::chrono::milliseconds { milliseconds } };
	while ( std::chrono::steady_clock::now() < deadline )
	{
		if ( !acceptsWork() ) co_return std::unexpected( "The download session is closed" );

		co_await folly::coro::sleep(
			std::chrono::duration_cast< std::chrono::microseconds >( std::min(
				deadline - std::chrono::steady_clock::now(),
				std::chrono::duration_cast< std::chrono::steady_clock::duration >( std::chrono::milliseconds {
					50 } ) ) ),
			&downloaderTimekeeper() );
	}
	co_return std::expected< void, std::string > {};
}

void SessionContext::Impl::startImportDirect(
	ScriptExecution& execution,
	Json::Value options,
	std::string chunk,
	PendingPromise promise )
{
	mergeFollowTags( options[ "tags" ], execution.tags );
	startHostOperation(
		execution,
		promise,
		importDirectOnHost( execution.work_id, execution.parser_url, std::move( options ), std::move( chunk ) ) );
}

SessionContext::Impl::HostTask SessionContext::Impl::abortDirectImports( const WorkID owner )
{
	std::vector< DirectImport > abandoned {};
	{
		const std::scoped_lock lock { m_mutex };
		for ( auto it { m_direct_imports.begin() }; it != m_direct_imports.end(); )
		{
			if ( it->first.first != owner )
			{
				++it;
				continue;
			}
			abandoned.emplace_back( std::move( it->second ) );
			it = m_direct_imports.erase( it );
		}
	}
	for ( auto& state : abandoned )
	{
		state.sink->abort();
		reportImportFailure( state.work, state.url, "Direct import abandoned", owner );
	}
	co_return;
}

folly::coro::Task< std::expected< void, std::string > > SessionContext::Impl::importDirectOnHost(
	const WorkID owner,
	std::string source,
	Json::Value options,
	std::string chunk )
{
	const auto key { std::pair { owner, options[ "id" ].asUInt64() } };
	const auto operation { options[ "operation" ].asString() };
	if ( operation == "abort" )
	{
		std::optional< DirectImport > aborted {};
		{
			const std::scoped_lock lock { m_mutex };
			auto entry { m_direct_imports.extract( key ) };
			if ( !entry.empty() ) aborted.emplace( std::move( entry.mapped() ) );
		}

		if ( aborted.has_value() )
		{
			aborted->sink->abort();
			reportImportFailure( aborted->work, aborted->url, "Direct import aborted by producer", owner );
		}

		co_return std::expected< void, std::string > {};
	}

	if ( m_cancelled.load() || m_closed.load() )
	{
		co_await abortDirectImports( owner );
		co_return std::unexpected( "The download session is closed" );
	}

	// The node stays valid without the lock because only this producer's lane erases its streams.
	DirectImport* found {};
	{
		const std::scoped_lock lock { m_mutex };
		const auto entry { m_direct_imports.find( key ) };
		if ( entry != m_direct_imports.end() ) found = &entry->second;
	}

	if ( operation == "open" )
	{
		if ( found != nullptr ) co_return std::unexpected( "Direct import already open" );

		if ( m_environment.imports == nullptr ) co_return std::unexpected( "This host cannot import files" );

		if ( options.isMember( "request" ) )
			co_return std::unexpected( "Direct imports do not accept an HTTP request" );

		if ( options.isMember( "url" ) && !options[ "url" ].isString() )
			co_return std::unexpected( "Direct import url must be a string" );

		const auto url { options.get( "url", source ).asString() };
		auto stored_url_e { m_environment.registry->normalizeUrl( url ) };
		if ( !stored_url_e.has_value() )
			co_return std::unexpected( "Direct import requires an absolute provenance URL: " + stored_url_e.error() );

		if ( options.isMember( "forceDownload" ) && !options[ "forceDownload" ].isBool() )
			co_return std::unexpected( "forceDownload must be boolean" );

		auto stored_options_e { m_environment.registry->normalizeOptionUrls( options ) };
		if ( !stored_options_e.has_value() ) co_return std::unexpected( stored_options_e.error() );

		auto& stored_options { stored_options_e.value() };
		const WorkID work { m_next_work.fetch_add( 1 ) };
		const auto& stored_url { stored_url_e.value().url };
		if ( m_options.observer != nullptr )
		{
			const auto queued_e { co_await m_options.observer->onImportQueued(
				WorkInfo { .id = work, .parent = owner, .url = stored_url } ) };
			if ( !queued_e.has_value() ) co_return std::unexpected( queued_e.error() );
		}
		std::vector< std::pair< std::string, std::string > > types;
		for ( const auto& entry : std::as_const( stored_options )[ "urls" ] )
			types.emplace_back( entry[ "url" ].asString(), entry[ "type" ].asString() );

		if ( m_options.observer != nullptr && !types.empty() )
		{
			const auto saved_e { co_await m_options.observer->onUrlTypesSet( std::move( types ) ) };
			if ( !saved_e.has_value() )
			{
				reportImportFailure( work, stored_url, saved_e.error(), owner );
				co_return std::unexpected( saved_e.error() );
			}
		}
		ImportRequest request {
			.work = work,
			.url = stored_url,
			.options = std::move( stored_options ),
			.session_id = m_options.session_id,
			.force_download = m_options.force_download || m_environment.config.force_download
			               || options.get( "forceDownload", false ).asBool()
		};
		request.options.removeMember( "id" );
		request.options.removeMember( "operation" );
		auto sink_e { m_environment.imports->open( request ) };
		if ( !sink_e.has_value() )
		{
			reportImportFailure( work, stored_url, sink_e.error(), owner, FailureStage::IMPORT );
			co_return std::unexpected( sink_e.error() );
		}
		DirectImport state { .sink = std::move( sink_e.value() ), .work = work, .url = stored_url, .metadata = {} };
		if ( options[ "postedAt" ].isInt64() ) state.posted_at = options[ "postedAt" ].asInt64();
		if ( options[ "filename" ].isString() ) state.metadata.filename = options[ "filename" ].asString();

		{
			const std::scoped_lock lock { m_mutex };
			m_direct_imports.emplace( key, std::move( state ) );
		}
		co_return std::expected< void, std::string > {};
	}
	if ( found == nullptr ) co_return std::unexpected( "Direct import is not open" );

	auto& state { *found };
	bool success {};
	std::string failure { "Direct import failed" };
	const auto cleanup { folly::makeGuard(
		[ & ]
		{
			if ( success ) return;

			state.sink->abort();
			reportImportFailure( state.work, state.url, failure, owner, FailureStage::IMPORT );
			const std::scoped_lock lock { m_mutex };
			m_direct_imports.erase( key );
		} ) };
	try
	{
		if ( operation == "write" )
		{
			const auto written_e {
				co_await state.sink->write( std::as_bytes( std::span { chunk.data(), chunk.size() } ) )
			};
			if ( !written_e.has_value() )
			{
				failure = written_e.error();
				co_return std::unexpected( failure );
			}
			state.metadata.size += chunk.size();
			success = true;
			co_return std::expected< void, std::string > {};
		}
		if ( operation != "finish" ) co_return std::unexpected( "Unknown direct import operation" );

		std::expected< ImportResult, std::string > imported_e { co_await state.sink->finish( state.metadata ) };

		if ( !imported_e.has_value() )
		{
			failure = imported_e.error();
			co_return std::unexpected( failure );
		}
		const auto& imported { imported_e.value() };
		const ImportInfo info {
			.work = state.work,
			.url = state.url,
			.record_id = imported.record_id,
			.size = state.metadata.size,
			.filename = state.metadata.filename,
			.note = imported.note,
			.existing = imported.existing,
			.posted_at = state.posted_at
		};
		m_diagnostics.recordImported( info );
		if ( m_options.observer != nullptr )
		{
			co_await m_options.observer->onImported( info );
		}

		success = true;
		{
			const std::scoped_lock lock { m_mutex };
			m_direct_imports.erase( key );
		}
		co_return std::expected< void, std::string > {};
	}
	catch ( const std::exception& error )
	{
		failure = error.what();
		co_return std::unexpected( failure );
	}
}

} // namespace idhan::downloader
