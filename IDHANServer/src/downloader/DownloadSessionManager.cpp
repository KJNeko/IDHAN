#include <folly/coro/BlockingWait.h>
#include "DownloadSessionManager.hpp"


#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <sstream>
#include <json/json.h>
#include <unordered_set>
#include <utility>

#include "Config.hpp"
#include "DatabaseSecrets.hpp"
#include "DatabaseParserHosts.hpp"
#include "DownloadImports.hpp"
#include "DownloadSessionEventHub.hpp"
#include "SessionRowObserver.hpp"
#include "idhan/db/DBContext.hpp"
#include "logging/format_ns.hpp"
#include "logging/log.hpp"
#include "downloaderEvents.hpp"
#include "sessionRows.hpp"

namespace idhan::downloader
{

db::DBContext& DownloadSessionManager::dbContext()
{
	return db::DBContext::instance();
}

runtime::Task< std::expected< std::vector< std::expected< NormalizedUrl, std::string > >, std::string > >
	DownloadSessionManager::normalizeUrls( std::vector< std::string > urls )
{
	const auto initialized_e { co_await initialize() };
	if ( !initialized_e.has_value() ) co_return std::unexpected( initialized_e.error() );

	const std::scoped_lock lock { m_mutex };
	if ( m_stopped.load() ) co_return std::unexpected( "The downloader is shutting down" );

	std::vector< std::expected< NormalizedUrl, std::string > > normalized {};
	normalized.reserve( urls.size() );
	for ( const auto& url : urls ) normalized.emplace_back( m_downloader->normalizeUrl( url ) );

	co_return normalized;
}

DownloadSessionManager::~DownloadSessionManager()
{
	// Fallback for process teardown; normal shutdown awaits the coroutine.
	folly::coro::blockingWait( shutdown() );
}

runtime::Task< std::expected< ParserUpdateReport, std::string > > DownloadSessionManager::prepareParsers()
{
	const auto initialized_e { co_await initialize() };

	if ( !initialized_e.has_value() ) co_return std::unexpected( initialized_e.error() );

	const std::scoped_lock lock { m_mutex };

	if ( m_stopped.load() ) co_return std::unexpected( "The downloader is shutting down" );

	co_return m_downloader->parserStatus();
}

runtime::Task< std::expected< ParserUpdateReport, std::string > > DownloadSessionManager::updateParsers()
{
	const auto initialized_e { co_await initialize() };

	if ( !initialized_e.has_value() ) co_return std::unexpected( initialized_e.error() );

	if ( m_stopped.load() ) co_return std::unexpected( "The downloader is shutting down" );

	std::shared_ptr< DatabaseSecrets > secrets;
	{
		const std::scoped_lock lock { m_mutex };
		secrets = m_secrets;
	}
	if ( !secrets ) co_return std::unexpected( "The downloader is shutting down" );

	const auto reloaded_e { co_await secrets->reload() };
	if ( !reloaded_e.has_value() ) co_return std::unexpected( reloaded_e.error() );

	const std::scoped_lock lock { m_mutex };
	if ( m_stopped.load() ) co_return std::unexpected( "The downloader is shutting down" );

	co_return m_downloader->updateParsers();
}

runtime::Task< std::expected< ParserUpdateReport, std::string > > DownloadSessionManager::updateParser(
	std::filesystem::path path )
{
	const auto initialized_e { co_await initialize() };

	if ( !initialized_e.has_value() ) co_return std::unexpected( initialized_e.error() );

	const std::scoped_lock lock { m_mutex };

	if ( m_stopped.load() ) co_return std::unexpected( "The downloader is shutting down" );

	co_return m_downloader->updateParser( path );
}

runtime::Task< std::expected< void, std::string > > DownloadSessionManager::initialize()
{
	const auto initialize_lock { co_await m_initialize_mutex.co_scoped_lock() };
	{
		const std::scoped_lock lock { m_mutex };
		if ( m_downloader != nullptr ) co_return std::expected< void, std::string > {};

		if ( m_stopped.load() ) co_return std::unexpected( "The downloader is shutting down" );
	}

	const std::filesystem::path parser_directory {
		config::get< std::string >( "downloader", "parser_directory", "./downloaders/parsers" )
	};

	const auto initialize_started { std::chrono::steady_clock::now() };
	log::info( "Downloader initializing, parsers: {}", std::filesystem::absolute( parser_directory ).string() );

	DownloaderConfig configuration {};
	configuration.parser_directory = parser_directory;
	configuration.watch_scripts = config::get< bool >( "downloader", "watch_scripts", true );
	configuration.script_watch_interval = std::chrono::milliseconds {
		config::get< std::size_t >( "downloader", "script_watch_interval_ms", std::size_t { 500 } )
	};
	configuration.script_watch_debounce = std::chrono::milliseconds {
		config::get< std::size_t >( "downloader", "script_watch_debounce_ms", std::size_t { 250 } )
	};
	const auto options_json { config::get< std::string >( "downloader", "options_json", "{}" ) };
	std::istringstream options_stream { options_json };
	Json::CharReaderBuilder options_reader;
	std::string options_errors;

	if ( !Json::parseFromStream( options_reader, options_stream, &configuration.options, &options_errors ) )
		co_return std::unexpected( "Invalid downloader options_json: " + options_errors );

	if ( !configuration.options.isObject() ) co_return std::unexpected( "Downloader options_json must be an object" );
	configuration.url_classes = config::get< std::string >( "downloader", "url_classes", std::string {} );
	configuration.io_threads = config::get< std::size_t >( "downloader", "io_threads", std::size_t {} );
	configuration.lane_keep_alive = std::chrono::seconds {
		config::get< std::size_t >( "downloader", "lane_keep_alive_seconds", std::size_t { 30 } )
	};
	configuration.lane_error_backoff = std::chrono::seconds {
		config::get< std::size_t >( "downloader", "lane_error_backoff_seconds", std::size_t { 30 } )
	};
	configuration.lane_concurrency =
		std::max< std::size_t >( 1, config::get< std::size_t >( "downloader", "lane_concurrency", std::size_t { 1 } ) );
	configuration.session_inflight_requests =
		config::get< std::size_t >( "downloader", "session_inflight_requests", std::size_t { 64 } );
	configuration.import_threads = config::get< std::size_t >( "downloader", "import_threads", std::size_t { 4 } );
	configuration.parser_instances = config::get< std::size_t >( "downloader", "parser_instances", std::size_t { 1 } );
	configuration.worker_memory_limit =
		config::get< std::size_t >( "downloader", "worker_memory_limit", std::size_t { 256 * 1024 * 1024 } );
	configuration.worker_stack_limit =
		config::get< std::size_t >( "downloader", "worker_stack_limit", std::size_t { 1024 * 1024 } );
	configuration.script_burst_timeout = std::chrono::milliseconds {
		config::get< std::size_t >( "downloader", "script_burst_timeout_ms", std::size_t { 5000 } )
	};
	configuration.max_response_bytes =
		config::get< std::size_t >( "downloader", "max_response_bytes", std::size_t { 32 * 1024 * 1024 } );
	configuration.user_agent = config::get< std::string >( "downloader", "user_agent", std::string {} );
	configuration.force_download = config::get< bool >( "downloader", "force_download", false );

	const std::string version { config::get< std::string >( "downloader", "http_version", "3" ) };
	const auto parser_instances_logged { configuration.parser_instances };
	const bool force_download_logged { configuration.force_download };

	if ( version == "1.1" )
		configuration.http_version = HttpVersion::HTTP_1_1;
	else if ( version == "2" )
		configuration.http_version = HttpVersion::HTTP_2;
	else
		configuration.http_version = HttpVersion::HTTP_3;

	auto secrets { std::make_shared< DatabaseSecrets >() };
	const auto loaded_secrets_e { co_await secrets->reload() };
	if ( !loaded_secrets_e.has_value() ) co_return std::unexpected( loaded_secrets_e.error() );

	auto imports { std::make_unique< DownloadImports >() };
	auto cookies { std::make_unique< DatabaseCookies >() };
	auto parser_hosts { std::make_unique< DatabaseParserHosts >( dbContext() ) };

	auto downloader { co_await DownloaderContext::create(
		std::move( configuration ),
		DownloaderHost {
			.imports = imports.get(),
			.cookies = cookies.get(),
			.secrets = secrets.get(),
			.parser_hosts = parser_hosts.get() } ) };

	if ( !downloader )
	{
		log::error( "Downloader initialization failed: {}", downloader.error() );
		co_return std::unexpected( downloader.error() );
	}

	{
		const std::scoped_lock lock { m_mutex };
		if ( m_stopped.load() ) co_return std::unexpected( "The downloader is shutting down" );

		m_imports = std::move( imports );
		m_cookies = std::move( cookies );
		m_secrets = std::move( secrets );
		m_parser_hosts = std::move( parser_hosts );
		m_downloader = std::move( *downloader );
	}
	log::info(
		"Downloader initialized in {} ms ({} instance(s) per parser, HTTP {}, force_download {})",
		std::chrono::duration_cast< std::chrono::milliseconds >( std::chrono::steady_clock::now() - initialize_started )
			.count(),
		parser_instances_logged,
		version,
		force_download_logged );
	co_return std::expected< void, std::string > {};
}

runtime::Task< std::shared_ptr< DownloadSessionManager::Session > > DownloadSessionManager::sessionFor(
	const DownloadSessionID session_id,
	Json::Value settings )
{
	const auto creation_lock { co_await m_session_creation_mutex.co_scoped_lock() };

	std::shared_ptr< Session > session {};
	{
		const std::scoped_lock lock { m_mutex };
		if ( m_stopped.load() || m_downloader == nullptr ) co_return nullptr;

		const auto found { m_sessions.find( session_id ) };
		if ( found != m_sessions.end() ) co_return found->second;
	}

	session = std::make_shared< Session >();
	session->id = session_id;
	session->observer = std::make_unique< SessionRowObserver >( session_id );
	auto saved_e { co_await rows::savedUrls( session_id, dbContext() ) };
	if ( !saved_e.has_value() )
	{
		log::error( "Unable to restore downloader session {}: {}", session_id, saved_e.error() );
		co_return nullptr;
	}

	session->observer->restore( std::move( saved_e.value() ) );
	const std::scoped_lock lock { m_mutex };
	if ( m_stopped.load() || m_downloader == nullptr ) co_return nullptr;

	session->context = m_downloader->createSession(
		SessionOptions {
			.parser_options =
				settings.isMember( "parsers" ) ? settings[ "parsers" ] : Json::Value { Json::objectValue },
			.force_import = settings.isMember( "force_import" ) ?
			                    std::optional< bool > { settings[ "force_import" ].asBool() } :
			                    std::nullopt,
			.root_url = {},
			.observer = session->observer.get(),
			.session_id = session_id,
			.generator = settings[ "generator" ] } );

	if ( !session->context )
	{
		session.reset();
		co_return session;
	}

	m_sessions.emplace( session_id, session );
	co_return session;
}

runtime::Task< std::expected< void, std::string > > DownloadSessionManager::submit(
	const DownloadSessionUrlID job_id,
	const DownloadSessionID session_id,
	std::string url,
	std::string settings_json,
	const std::optional< std::int64_t > last_used_at,
	const bool recovering )
{
	Json::Value settings;
	Json::CharReaderBuilder reader;
	std::string errors;
	std::istringstream input { settings_json };
	if ( !Json::parseFromStream( reader, input, &settings, &errors ) || !settings.isObject() )
		co_return std::unexpected( "Invalid download settings" );

	if ( const auto initialized { co_await initialize() }; !initialized )
	{
		log::warn( "Downloader job {} rejected: {}", job_id, initialized.error() );
		co_return std::unexpected( initialized.error() );
	}

	// A persisted subscription requires its site parser, including after recovery. Without this,
	// an unclaimed search URL falls back to downloading its response as a direct file.
	const auto& required_parser { settings[ "required_parser" ] };
	if ( !required_parser.isNull() )
	{
		if ( !required_parser.isString() ) co_return std::unexpected( "required_parser must be a parser ID" );

		const auto parsers_e { co_await parsers() };
		if ( !parsers_e.has_value() ) co_return std::unexpected( parsers_e.error() );

		const auto& available { parsers_e.value() };
		bool found {};
		for ( const auto& parser : available )
		{
			if ( parser[ "id" ] == required_parser ) found = true;
		}

		if ( !found )
			co_return std::unexpected( "Required downloader parser is not available: " + required_parser.asString() );
	}

	if ( settings[ "generator" ].isObject() )
	{
		const auto& invocation { settings[ "generator" ] };
		const auto available_e { co_await parsers() };
		if ( !available_e.has_value() ) co_return std::unexpected( available_e.error() );

		const auto& available { available_e.value() };
		std::string origin { invocation.get( "origin", "" ).asString() };
		for ( const auto& parser : available )
		{
			if ( parser[ "id" ] != invocation[ "parser" ] ) continue;

			for ( const auto& url_class : parser[ "urlClasses" ] )
			{
				for ( const auto& generator : url_class[ "generators" ] )
				{
					if ( generator[ "handler" ] != invocation[ "handler" ] ) continue;

					for ( const auto& host : url_class[ "hosts" ] )
					{
						if ( !origin.empty() ) break;

						const auto hostname { host.isString() ? host.asString() : host.get( "host", "" ).asString() };
						if ( hostname.empty() || hostname.contains( '*' ) ) continue;

						origin = "https://" + hostname;
					}
				}
			}
		}
		if ( origin.empty() ) co_return std::unexpected( "This generator requires an origin" );

		url = std::move( origin );
	}

	const auto session { co_await sessionFor( session_id, settings ) };

	if ( !session ) co_return std::unexpected( "The downloader is shutting down" );

	if ( !recovering ) session->observer->discardSaved( job_id );

	WorkID reserved_work {};
	auto submitted { session->context->submit(
		url,
		std::nullopt,
		[ & ]( const WorkID work )
		{
			reserved_work = work;
			session->observer->adopt( work, job_id );
			DownloadSessionEventHub::instance().started( session_id, last_used_at );
		} ) };

	if ( !submitted )
	{
		if ( reserved_work != 0 ) session->observer->forget( reserved_work );

		log::warn( "Downloader job {} rejected: {}", job_id, submitted.error() );
		co_return std::unexpected( std::move( submitted.error() ) );
	}

	log::info( "Downloader job {} queued for session {}: {}", job_id, session_id, url );
	co_return std::expected< void, std::string > {};
}

runtime::Task< std::string > DownloadSessionManager::sessionSettings(
	db::DBContext& db,
	const DownloadSessionID session_id )
{
	db::DBBindings bindings;
	bindings.bind( session_id );
	const auto rows {
		co_await db
			.query(
				"SELECT options::text FROM download_sessions WHERE download_session_id = $1", std::move( bindings ) )
			.rows< std::string >()
	};
	if ( rows.empty() ) co_return "{}";

	co_return std::get< 0 >( rows.row( 0 ) );
}

runtime::Task< std::expected< Json::Value, std::string > > DownloadSessionManager::downloadOptions()
{
	const auto initialized_e { co_await initialize() };
	if ( !initialized_e.has_value() ) co_return std::unexpected( initialized_e.error() );

	const std::scoped_lock lock { m_mutex };
	if ( m_downloader == nullptr ) co_return std::unexpected( "The downloader is shutting down" );

	co_return m_downloader->downloadOptions();
}

runtime::Task< std::expected< void, std::string > > DownloadSessionManager::installParser(
	std::string filename,
	std::string source,
	bool replace )
{
	const auto initialized_e { co_await initialize() };
	if ( !initialized_e.has_value() ) co_return std::unexpected( initialized_e.error() );

	const std::scoped_lock lock { m_mutex };
	if ( !m_downloader ) co_return std::unexpected( "Downloader unavailable" );

	co_return m_downloader->installParser( std::move( filename ), std::move( source ), replace );
}

runtime::Task< std::expected< std::string, std::string > > DownloadSessionManager::parserSource( std::string filename )
{
	const auto initialized_e { co_await initialize() };
	if ( !initialized_e.has_value() ) co_return std::unexpected( initialized_e.error() );

	const std::scoped_lock lock { m_mutex };
	if ( !m_downloader ) co_return std::unexpected( "Downloader unavailable" );

	co_return m_downloader->parserSource( std::move( filename ) );
}

runtime::Task< std::expected< Json::Value, std::string > > DownloadSessionManager::parsers()
{
	const auto initialized_e { co_await initialize() };
	if ( !initialized_e.has_value() ) co_return std::unexpected( initialized_e.error() );

	const std::scoped_lock lock { m_mutex };
	if ( m_downloader == nullptr ) co_return std::unexpected( "The downloader is shutting down" );

	co_return m_downloader->parsers();
}

std::vector< std::pair< DownloadSessionID, std::uint64_t > > DownloadSessionManager::downloadedBytes()
{
	const std::scoped_lock lock { m_mutex };
	std::vector< std::pair< DownloadSessionID, std::uint64_t > > totals;
	totals.reserve( m_sessions.size() );
	for ( const auto& [ id, session ] : m_sessions )
	{
		if ( session->context ) totals.emplace_back( id, session->context->downloadedBytes() );
	}
	return totals;
}

runtime::Task< std::vector< DownloadSessionManager::ResumableRoot > > DownloadSessionManager::resumable(
	db::DBContext& db )
{
	const auto roots {
		co_await db
			.query(
				"WITH RECURSIVE unfinished AS ("
				"    SELECT download_session_id, download_session_url_id, parent_url_id FROM download_session_urls"
				"     WHERE state IN ('pending', 'processing')"
				"  UNION ALL"
				"    SELECT parent.download_session_id, parent.download_session_url_id, parent.parent_url_id"
				"      FROM download_session_urls parent"
				"      JOIN unfinished child ON parent.download_session_id = child.download_session_id"
				"       AND parent.download_session_url_id = child.parent_url_id"
				") "
				"SELECT DISTINCT root.download_session_url_id, root.download_session_id, root.url, sessions.options::text "
				"FROM download_session_urls root "
				"JOIN download_sessions sessions USING (download_session_id) "
				"JOIN unfinished ON unfinished.download_session_id = root.download_session_id "
				"AND unfinished.download_session_url_id = root.download_session_url_id "
				"WHERE root.parent_url_id IS NULL AND NOT sessions.is_subscription "
				"ORDER BY root.download_session_id, root.download_session_url_id" )
			.rows< DownloadSessionUrlID, DownloadSessionID, std::string, std::string >()
	};

	std::vector< ResumableRoot > pending {};
	pending.reserve( roots.size() );

	for ( auto [ url_id, session_id, url, settings ] : roots )
	{
		// Replayed work reuses the saved tree, including each import's original outcome.
		pending.emplace_back( ResumableRoot { url_id, session_id, std::move( url ), std::move( settings ) } );
	}

	co_return pending;
}

runtime::Task< void > DownloadSessionManager::markUnresumable(
	ResumableRoot root,
	std::string error,
	db::DBContext& db )
{
	const auto stage { failureStage( FailureStage::CLOSED ) };
	const auto failed_e { co_await rows::fail( root.session_id, root.url_id, stage, error, db ) };
	if ( !failed_e.has_value() ) throw std::runtime_error( failed_e.error() );

	events::urlUpdated(
		root.session_id,
		root.url_id,
		events::UrlState {
			.state = "failed", .stage = stage, .error = std::move( error ), .finished_at = failed_e.value() } );
}

runtime::Task< std::expected< void, std::string > > DownloadSessionManager::setParserHost(
	std::string host,
	std::string parser )
{
	const auto initialized_e { co_await initialize() };
	if ( !initialized_e.has_value() ) co_return std::unexpected( initialized_e.error() );

	const std::scoped_lock lock { m_mutex };
	if ( m_downloader == nullptr ) co_return std::unexpected( "Downloader is shutting down" );

	co_return m_downloader->setParserHost( std::move( host ), std::move( parser ) );
}

void DownloadSessionManager::removeParserHost( const std::string_view host )
{
	const std::scoped_lock lock { m_mutex };
	if ( m_downloader != nullptr ) m_downloader->removeParserHost( host );
}

bool DownloadSessionManager::idle( const DownloadSessionID id )
{
	std::shared_ptr< Session > session;
	{
		const std::scoped_lock lock { m_mutex };
		const auto found { m_sessions.find( id ) };
		if ( found == m_sessions.end() ) return true;

		session = found->second;
	}
	return session->context->idle();
}

SessionSnapshot DownloadSessionManager::networkSnapshot( const DownloadSessionID session_id )
{
	std::shared_ptr< Session > session;
	{
		const std::lock_guard lock { m_mutex };
		const auto found { m_sessions.find( session_id ) };
		if ( found == m_sessions.end() ) return {};

		session = found->second;
	}
	auto snapshot { session->context->snapshot() };

	return snapshot;
}

std::vector< UrlTransfer > DownloadSessionManager::transfers(
	const DownloadSessionID session_id,
	const SessionSnapshot& snapshot )
{
	std::shared_ptr< Session > session;
	{
		const std::lock_guard lock { m_mutex };
		const auto found { m_sessions.find( session_id ) };
		if ( found == m_sessions.end() ) return {};

		session = found->second;
	}

	std::unordered_map< DownloadSessionUrlID, UrlTransfer > grouped;
	std::unordered_set< DownloadSessionUrlID > unsized;
	for ( const auto& request : snapshot.requests )
	{
		if ( request.host ) continue;

		const auto url_id { session->observer->rowFor( request.work ) };
		if ( url_id == 0 ) continue;

		auto& transfer { grouped.try_emplace( url_id, UrlTransfer { .url_id = url_id } ).first->second };
		transfer.bytes += request.bytes;
		transfer.total += request.total;
		if ( request.total == 0 ) unsized.insert( url_id );
	}

	std::vector< UrlTransfer > result;
	result.reserve( grouped.size() );
	for ( auto& [ url_id, transfer ] : grouped )
	{
		if ( unsized.contains( url_id ) ) transfer.total = 0;

		result.push_back( transfer );
	}
	return result;
}

std::unordered_map< DownloadSessionUrlID, std::string > DownloadSessionManager::liveStates(
	const DownloadSessionID session_id )
{
	std::shared_ptr< Session > session;
	{
		const std::lock_guard lock { m_mutex };
		const auto found { m_sessions.find( session_id ) };
		if ( found == m_sessions.end() ) return {};

		session = found->second;
	}
	return session->observer->liveStates();
}

runtime::Task< void > DownloadSessionManager::destroy( const DownloadSessionID session_id )
{
	std::shared_ptr< Session > removed {};

	{
		const std::scoped_lock lock { m_mutex };
		const auto found { m_sessions.find( session_id ) };

		if ( found == m_sessions.end() ) co_return;

		removed = std::move( found->second );
		m_sessions.erase( found );
	}

	if ( removed->context )
	{
		removed->context->cancel();
		co_await removed->context->wait();
		removed->context->close();
	}
}

runtime::Task< void > DownloadSessionManager::shutdown()
{
	const auto shutdown_lock { co_await m_shutdown_mutex.co_scoped_lock() };
	if ( m_stopped.exchange( true ) ) co_return;

	const auto initialize_lock { co_await m_initialize_mutex.co_scoped_lock() };
	const auto creation_lock { co_await m_session_creation_mutex.co_scoped_lock() };

	std::unordered_map< DownloadSessionID, std::shared_ptr< Session > > sessions {};
	std::unique_ptr< DownloaderContext > downloader {};

	{
		const std::scoped_lock lock { m_mutex };
		sessions.swap( m_sessions );
		downloader = std::move( m_downloader );
	}

	if ( downloader ) co_await downloader->shutdown();

	// Workers and transfer callbacks borrow the observers owned by these sessions.
	sessions.clear();

	downloader.reset();

	{
		const std::scoped_lock lock { m_mutex };
		m_imports.reset();
		m_secrets.reset();
		m_cookies.reset();
		m_parser_hosts.reset();
	}
}

runtime::Task< std::expected< void, std::string > > DownloadSessionManager::resetBackoff( std::string lane_key )
{
	if ( const auto initialized { co_await initialize() }; !initialized )
		co_return std::unexpected( initialized.error() );

	const std::scoped_lock lock { m_mutex };

	if ( m_downloader == nullptr ) co_return std::unexpected( "The downloader is not running" );

	if ( lane_key.empty() )
		m_downloader->resetAllBackoff();
	else
		m_downloader->resetBackoff( lane_key );

	co_return std::expected< void, std::string > {};
}

std::vector< LaneSnapshot > DownloadSessionManager::laneSnapshots()
{
	const std::scoped_lock lock { m_mutex };

	return m_downloader == nullptr ? std::vector< LaneSnapshot > {} : m_downloader->laneSnapshots();
}

ParserCacheSnapshot DownloadSessionManager::cacheSnapshot()
{
	const std::scoped_lock lock { m_mutex };
	return m_downloader == nullptr ? ParserCacheSnapshot {} : m_downloader->cacheSnapshot();
}

runtime::Task< std::expected< std::unordered_map< std::string, std::string >, std::string > > DownloadSessionManager::
	secrets()
{
	const auto initialized { co_await initialize() };

	if ( !initialized ) co_return std::unexpected( initialized.error() );

	std::shared_ptr< DatabaseSecrets > secrets;

	{
		const std::scoped_lock lock { m_mutex };

		if ( m_downloader == nullptr || m_secrets == nullptr )
			co_return std::unexpected( "The downloader is not running" );

		secrets = m_secrets;
	}

	co_return co_await secrets->values();
}

runtime::Task< std::expected< void, std::string > > DownloadSessionManager::setSecrets(
	std::unordered_map< std::string, std::string > values )
{
	const auto initialized { co_await initialize() };

	if ( !initialized ) co_return std::unexpected( initialized.error() );

	std::shared_ptr< DatabaseSecrets > secrets;

	{
		const std::scoped_lock lock { m_mutex };

		if ( m_downloader == nullptr || m_secrets == nullptr )
			co_return std::unexpected( "The downloader is not running" );

		secrets = m_secrets;
	}

	co_return co_await secrets->set( std::move( values ) );
}

DownloadSessionManager& downloadSessionManager()
{
	static DownloadSessionManager manager {};
	return manager;
}

} // namespace idhan::downloader
