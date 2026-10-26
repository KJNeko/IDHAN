#include "DownloadSessionManager.hpp"

#include <folly/coro/BlockingWait.h>

#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <utility>

#include "Config.hpp"
#include "DatabaseSecrets.hpp"
#include "DownloadImports.hpp"
#include "DownloadSessionEventHub.hpp"
#include "RateLimitEventHub.hpp"
#include "SessionRowObserver.hpp"
#include "idhan/db/DBContext.hpp"
#include "logging/format_ns.hpp"
#include "logging/log.hpp"
#include "sessionRows.hpp"

namespace idhan::downloader
{

db::DBContext& DownloadSessionManager::dbContext()
{
	return db::DBContext::instance();
}

DownloadSessionUrlID DownloadSessionManager::addRow(
	const DownloadSessionID session_id,
	const DownloadSessionUrlID parent_row,
	const std::string& url,
	const std::string_view state,
	std::string message )
{
	auto result { folly::coro::blockingWait(
		rows::insert( session_id, parent_row, url, std::string { state }, std::move( message ), dbContext() ) ) };
	if ( !result ) throw std::runtime_error( result.error() );

	DownloadSessionEventHub::instance().notify( session_id );
	return *result;
}

std::expected< std::vector< std::string >, std::string > DownloadSessionManager::classStorageUrls(
	std::vector< std::string > urls )
{
	const auto initialized_e { initialize() };
	if ( !initialized_e.has_value() ) return std::unexpected( initialized_e.error() );

	const std::scoped_lock lock { m_mutex };
	if ( m_stopped.load() ) return std::unexpected( "The downloader is shutting down" );

	for ( auto& url : urls ) url = m_downloader->classStorageUrl( url );
	return urls;
}

void DownloadSessionManager::markRow(
	const DownloadSessionID session_id,
	const DownloadSessionUrlID row_id,
	const std::string_view state,
	std::string error )
{
	auto result {
		folly::coro::blockingWait( rows::mark( row_id, std::string { state }, std::move( error ), dbContext() ) )
	};
	if ( !result ) throw std::runtime_error( result.error() );

	DownloadSessionEventHub::instance().notify( session_id );
}

void DownloadSessionManager::addError(
	const DownloadSessionID session_id,
	const DownloadSessionUrlID row_id,
	const std::string& url,
	const std::string& lane,
	const std::optional< std::int32_t > status,
	std::string message )
{
	auto result { folly::coro::blockingWait(
		rows::addError( session_id, row_id, url, lane, status, std::move( message ), dbContext() ) ) };
	if ( !result ) throw std::runtime_error( result.error() );

	DownloadSessionEventHub::instance().notify( session_id );
}

DownloadSessionManager::~DownloadSessionManager()
{
	shutdown();
}

std::expected< void, std::string > DownloadSessionManager::initialize()
{
	const std::scoped_lock lock { m_mutex };

	if ( m_downloader != nullptr ) return {};
	if ( m_stopped.load() ) return std::unexpected( "The downloader is shutting down" );

	const std::filesystem::path parser_directory {
		config::get< std::string >( "downloader", "parser_directory", IDHAN_DOWNLOADER_DEFAULT_PARSER_DIRECTORY )
	};

	DownloaderConfig configuration {};
	configuration.parser_directory = parser_directory;
	configuration.url_classes = config::get< std::string >( "downloader", "url_classes", std::string {} );
	configuration.io_threads = config::get< std::size_t >( "downloader", "io_threads", std::size_t {} );
	configuration.lane_keep_alive = std::chrono::seconds {
		config::get< std::size_t >( "downloader", "lane_keep_alive_seconds", std::size_t { 30 } )
	};
	configuration.lane_error_backoff = std::chrono::seconds {
		config::get< std::size_t >( "downloader", "lane_error_backoff_seconds", std::size_t { 30 } )
	};
	configuration.max_shards_per_lane =
		config::get< std::size_t >( "downloader", "max_shards_per_lane", std::size_t { 4 } );
	configuration.session_inflight_requests =
		config::get< std::size_t >( "downloader", "session_inflight_requests", std::size_t { 64 } );
	configuration.script_threads = config::get< std::size_t >( "downloader", "script_threads", std::size_t { 4 } );
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
	configuration.flags = config::getBoolTable( "downloader.flags" );
	configuration.force_download = config::get< bool >( "downloader", "force_download", false );

	const std::string version { config::get< std::string >( "downloader", "http_version", "3" ) };

	if ( version == "1.1" )
		configuration.http_version = HttpVersion::HTTP_1_1;
	else if ( version == "2" )
		configuration.http_version = HttpVersion::HTTP_2;
	else
		configuration.http_version = HttpVersion::HTTP_3;

	for ( const auto& [ name, enabled ] : configuration.flags ) log::info( "Downloader flag {} = {}", name, enabled );

	m_tag_domain = config::get< std::string >( "downloader", "tag_domain", "default" );

	m_secrets = std::make_unique< DatabaseSecrets >();
	m_imports = std::make_unique< DownloadImports >( m_tag_domain );
	m_cookies = std::make_unique< DatabaseCookies >();

	auto downloader { DownloaderContext::create(
		std::move( configuration ),
		DownloaderHost {
			.imports = m_imports.get(),
			.cookies = m_cookies.get(),
			.secrets = m_secrets.get(),
			.lanes = &RateLimitEventHub::instance() } ) };

	if ( !downloader ) return std::unexpected( downloader.error() );

	m_downloader = std::move( *downloader );
	log::info( "Downloader parsers: {}", parser_directory.string() );
	return {};
}

std::shared_ptr< DownloadSessionManager::Session > DownloadSessionManager::sessionFor(
	const DownloadSessionID session_id )
{
	const std::scoped_lock lock { m_mutex };

	if ( m_stopped.load() || m_downloader == nullptr ) return {};

	if ( const auto found { m_sessions.find( session_id ) }; found != m_sessions.end() ) return found->second;

	auto session { std::make_shared< Session >() };
	session->id = session_id;
	session->observer = std::make_unique< SessionRowObserver >( *this, session_id );
	session->context = m_downloader->createSession(
		SessionOptions { .root_url = {},
	                     .observer = session->observer.get(),
	                     .host_tag = static_cast< std::uint64_t >( session_id ) } );

	if ( !session->context ) return {};

	m_sessions.emplace( session_id, session );
	return session;
}

std::expected< void, std::string > DownloadSessionManager::submit(
	const DownloadSessionUrlID job_id,
	const DownloadSessionID session_id,
	std::string url )
{
	if ( const auto initialized { initialize() }; !initialized )
	{
		log::warn( "Downloader job {} rejected: {}", job_id, initialized.error() );
		return std::unexpected( initialized.error() );
	}

	const auto session { sessionFor( session_id ) };

	if ( !session ) return std::unexpected( "The downloader is shutting down" );

	WorkID reserved_work {};
	auto submitted { session->context->submit(
		url,
		std::nullopt,
		[ & ]( const WorkID work )
		{
			reserved_work = work;
			session->observer->adopt( work, job_id );
		} ) };

	if ( !submitted )
	{
		if ( reserved_work != 0 ) session->observer->forget( reserved_work );

		log::warn( "Downloader job {} rejected: {}", job_id, submitted.error() );
		return std::unexpected( std::move( submitted.error() ) );
	}

	log::info( "Downloader job {} queued for session {}: {}", job_id, session_id, url );
	return {};
}

runtime::Task< std::vector< DownloadSessionManager::ResumableRoot > > DownloadSessionManager::resumable(
	db::DBContext& db )
{
	const auto roots {
		co_await db
			.query(
				"WITH RECURSIVE unfinished AS ("
				"    SELECT download_session_url_id, parent_url_id FROM download_session_urls"
				"     WHERE state IN ('pending', 'processing')"
				"  UNION ALL"
				"    SELECT parent.download_session_url_id, parent.parent_url_id"
				"      FROM download_session_urls parent"
				"      JOIN unfinished child ON parent.download_session_url_id = child.parent_url_id"
				") "
				"SELECT DISTINCT root.download_session_url_id, root.download_session_id, root.url "
				"FROM download_session_urls root "
				"JOIN unfinished ON unfinished.download_session_url_id = root.download_session_url_id "
				"WHERE root.parent_url_id IS NULL ORDER BY root.download_session_url_id" )
			.rows< DownloadSessionUrlID, DownloadSessionID, std::string >()
	};

	std::vector< ResumableRoot > pending {};
	pending.reserve( roots.size() );

	for ( auto [ job_id, session_id, url ] : roots )
	{
		db::DBBindings job_binding {};
		job_binding.bind( job_id );
		co_await db.query( "DELETE FROM download_session_urls WHERE parent_url_id = $1", job_binding );
		co_await db.query(
			"UPDATE download_session_urls SET state = 'pending', finished_at = NULL, error = NULL "
			"WHERE download_session_url_id = $1",
			std::move( job_binding ) );

		pending.emplace_back( ResumableRoot { job_id, session_id, std::move( url ) } );
	}

	co_return pending;
}

runtime::Task< void > DownloadSessionManager::markUnresumable(
	const DownloadSessionUrlID job_id,
	std::string error,
	db::DBContext& db )
{
	db::DBBindings bindings {};
	bindings.bind( job_id );
	bindings.bind( std::move( error ) );
	co_await db.query(
		"UPDATE download_session_urls SET state = 'failed', finished_at = now(), error = $2 "
		"WHERE download_session_url_id = $1",
		std::move( bindings ) );
}

void DownloadSessionManager::destroy( const DownloadSessionID session_id )
{
	std::shared_ptr< Session > removed {};

	{
		const std::scoped_lock lock { m_mutex };
		const auto found { m_sessions.find( session_id ) };

		if ( found == m_sessions.end() ) return;

		removed = std::move( found->second );
		m_sessions.erase( found );
	}

	if ( removed->context )
	{
		removed->context->cancel();
		removed->context->wait();
		removed->context->close();
	}
}

void DownloadSessionManager::shutdown()
{
	if ( m_stopped.exchange( true ) ) return;

	std::unordered_map< DownloadSessionID, std::shared_ptr< Session > > sessions {};
	std::unique_ptr< DownloaderContext > downloader {};

	{
		const std::scoped_lock lock { m_mutex };
		sessions.swap( m_sessions );
		downloader = std::move( m_downloader );
	}

	for ( const auto& [ id, session ] : sessions )
	{
		if ( session->context ) session->context->close();
	}

	if ( downloader ) downloader->shutdown();

	// Workers and transfer callbacks borrow the observers owned by these sessions.
	sessions.clear();

	downloader.reset();

	{
		const std::scoped_lock lock { m_mutex };
		m_imports.reset();
		m_secrets.reset();
		m_cookies.reset();
	}
}

std::expected< void, std::string > DownloadSessionManager::resetBackoff( const std::string_view lane_key )
{
	if ( const auto initialized { initialize() }; !initialized ) return std::unexpected( initialized.error() );

	const std::scoped_lock lock { m_mutex };

	if ( m_downloader == nullptr ) return std::unexpected( "The downloader is not running" );

	if ( lane_key.empty() )
		m_downloader->resetAllBackoff();
	else
		m_downloader->resetBackoff( lane_key );

	return {};
}

std::vector< LaneSnapshot > DownloadSessionManager::laneSnapshots()
{
	const std::scoped_lock lock { m_mutex };

	return m_downloader == nullptr ? std::vector< LaneSnapshot > {} : m_downloader->laneSnapshots();
}

runtime::Task< std::expected< std::unordered_map< std::string, std::string >, std::string > > DownloadSessionManager::
	secrets()
{
	const auto initialized { initialize() };

	if ( !initialized ) co_return std::unexpected( initialized.error() );

	DatabaseSecrets* secrets {};

	{
		const std::scoped_lock lock { m_mutex };

		if ( m_downloader == nullptr || m_secrets == nullptr )
			co_return std::unexpected( "The downloader is not running" );

		secrets = m_secrets.get();
	}

	co_return co_await secrets->values();
}

runtime::Task< std::expected< void, std::string > > DownloadSessionManager::setSecrets(
	std::unordered_map< std::string, std::string > values )
{
	const auto initialized { initialize() };

	if ( !initialized ) co_return std::unexpected( initialized.error() );

	DatabaseSecrets* secrets {};

	{
		const std::scoped_lock lock { m_mutex };

		if ( m_downloader == nullptr || m_secrets == nullptr )
			co_return std::unexpected( "The downloader is not running" );

		secrets = m_secrets.get();
	}

	co_return co_await secrets->set( std::move( values ) );
}

std::vector< DownloadSessionManager::SessionDebugInfo > DownloadSessionManager::debugSnapshots(
	const std::unordered_map< DownloadSessionID, std::uint64_t >& cursors )
{
	std::vector< std::shared_ptr< Session > > sessions {};

	{
		const std::scoped_lock lock { m_mutex };
		sessions.reserve( m_sessions.size() );

		for ( const auto& [ id, session ] : m_sessions ) sessions.emplace_back( session );
	}

	std::vector< SessionDebugInfo > infos {};
	infos.reserve( sessions.size() );

	for ( const auto& session : sessions )
	{
		if ( session->context == nullptr ) continue;

		const auto cursor { cursors.find( session->id ) };

		infos.emplace_back(
			SessionDebugInfo {
				.id = session->id,
				.snapshot = session->context->snapshot( cursor == cursors.end() ? 0 : cursor->second ),
				.rows = session->observer == nullptr ? std::unordered_map< WorkID, DownloadSessionUrlID > {} :
		                                               session->observer->rows() } );
	}

	std::ranges::sort(
		infos, []( const SessionDebugInfo& left, const SessionDebugInfo& right ) { return left.id > right.id; } );

	return infos;
}

DownloadSessionManager& downloadSessionManager()
{
	static DownloadSessionManager manager {};
	return manager;
}

} // namespace idhan::downloader
