#include "DownloadSessionManager.hpp"

#include <json/json.h>
#include <sys/mman.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <span>
#include <unistd.h>
#include <utility>

#include "Config.hpp"
#include "DownloadSessionEvents.hpp"
#include "RateLimitEvents.hpp"
#include "db/drogonArrayBind.hpp"
#include "import/ImportFile.hpp"
#include "logging/format_ns.hpp"
#include "logging/log.hpp"
#include "tags/tags.hpp"
#include "urls/urls.hpp"

namespace idhan::downloader
{

static std::string downloaderResponseError( const std::string_view operation, const drogon::HttpResponsePtr& response )
{
	if ( response == nullptr ) return format_ns::format( "{} failed for an unreported reason", operation );

	return format_ns::format(
		"{} failed with HTTP {}: {}",
		operation,
		static_cast< int >( response->statusCode() ),
		response->body().substr( 0, 2048 ) );
}

class DownloadSessionManager::Imports final : public ImportSinkFactory
{
	class Sink final : public ImportSink
	{
		std::filesystem::path m_path {};
		std::FILE* m_file {};
		WorkID m_work {};
		std::string m_filename {};
		Json::Value m_options {};
		std::string m_url {};
		DownloadSessionID m_session_id {};
		std::string m_tag_domain {};
		bool m_discarded {};

		std::expected< void, std::string > ensureOpen()
		{
			if ( m_discarded ) return std::unexpected( "The import was already discarded" );

			if ( m_file != nullptr ) return {};

			std::filesystem::path path {
				std::filesystem::temp_directory_path() / format_ns::format( "idhan-download-{}-XXXXXX", m_work )
			};
			std::string name { path.string() };
			const int descriptor { ::mkstemp( name.data() ) };

			if ( descriptor < 0 ) return std::unexpected( "Unable to create a temporary file for the download" );

			m_file = ::fdopen( descriptor, "wb+" );

			if ( m_file == nullptr )
			{
				::close( descriptor );
				return std::unexpected( "Unable to open the temporary download file" );
			}

			m_path = std::filesystem::path { name };
			return {};
		}

		void discard()
		{
			m_discarded = true;

			if ( m_file != nullptr )
			{
				std::fclose( m_file );
				m_file = nullptr;
			}

			if ( !m_path.empty() )
			{
				std::error_code error {};
				std::filesystem::remove( m_path, error );
				m_path.clear();
			}
		}

	  public:

		Sink(
			const WorkID work,
			std::string filename,
			Json::Value options,
			std::string url,
			const DownloadSessionID session_id,
			std::string tag_domain ) :
		  m_work( work ),
		  m_filename( std::move( filename ) ),
		  m_options( std::move( options ) ),
		  m_url( std::move( url ) ),
		  m_session_id( session_id ),
		  m_tag_domain( std::move( tag_domain ) )
		{}

		Sink( const Sink& ) = delete;
		Sink& operator=( const Sink& ) = delete;

		~Sink() override { discard(); }

		std::expected< void, std::string > write( const std::span< const std::byte > bytes ) override
		{
			if ( const auto opened { ensureOpen() }; !opened ) return opened;

			if ( std::fwrite( bytes.data(), 1, bytes.size(), m_file ) != bytes.size() )
				return std::unexpected( format_ns::format( "Unable to buffer {}", m_url ) );

			return {};
		}

		std::expected< ImportResult, std::string > finish( const ImportMetadata& metadata ) override;

		void abort() override { discard(); }
	};

	std::string m_tag_domain {};

  public:

	explicit Imports( std::string tag_domain ) : m_tag_domain( std::move( tag_domain ) ) {}

	std::expected< std::unique_ptr< ImportSink >, std::string > open( const ImportRequest& request ) override
	{
		return std::make_unique< Sink >(
			request.work,
			request.options[ "filename" ].isString() ? request.options[ "filename" ].asString() : std::string {},
			request.options,
			request.url,
			static_cast< DownloadSessionID >( request.host_tag ),
			m_tag_domain );
	}
};

std::expected< ImportResult, std::string > DownloadSessionManager::Imports::Sink::finish(
	const ImportMetadata& metadata )
{
	const auto size { metadata.size };
	void* mapping { nullptr };

	if ( size != 0 )
	{
		if ( m_file == nullptr ) return std::unexpected( format_ns::format( "Nothing was written for {}", m_url ) );

		std::fflush( m_file );
		mapping = ::mmap( nullptr, size, PROT_READ, MAP_PRIVATE, ::fileno( m_file ), 0 );

		if ( mapping == MAP_FAILED )
		{
			discard();
			return std::unexpected( format_ns::format( "Unable to read the downloaded copy of {}", m_url ) );
		}
	}

	const std::span< const std::byte > data { static_cast< const std::byte* >( mapping ), size };
	const std::string filename { m_filename.empty() ? metadata.filename : m_filename };
	const std::string description { format_ns::format(
		"{} ({} bytes{})",
		m_url,
		size,
		metadata.content_type.empty() ? std::string {} : format_ns::format( ", {}", metadata.content_type ) ) };

	auto completed = [ & ]() -> drogon::Task< std::expected< ImportResult, std::string > >
	{
		const auto db { drogon::app().getDbClient() };
		const auto imported { co_await imports::importFile( data, filename, false, db ) };

		if ( !imported )
			co_return std::unexpected(
				downloaderResponseError( format_ns::format( "Importing {}", description ), imported.error() ) );

		if ( imported->status == ImportStatus::Failed )
			co_return std::unexpected(
				format_ns::format( "Downloaded file has an unrecognised MIME type: {}", description ) );

		if ( imported->status == ImportStatus::Deleted )
			co_return std::unexpected( format_ns::format( "Downloaded file was previously deleted: {}", description ) );

		std::vector< std::string > urls { m_url };

		if ( const auto& associated { m_options[ "urls" ] }; associated.isArray() )
		{
			for ( const auto& entry : associated )
			{
				if ( entry.isObject() && entry[ "url" ].isString() ) urls.emplace_back( entry[ "url" ].asString() );
			}
		}

		if ( const auto& discovered { m_options[ "discoveredUrls" ] }; discovered.isArray() )
		{
			for ( const auto& url : discovered )
			{
				if ( url.isString() ) urls.emplace_back( url.asString() );
			}
		}

		if ( const auto result { co_await helpers::associateUrls( imported->record_id, std::move( urls ), db ) };
		     !result )
			co_return std::unexpected( downloaderResponseError(
				format_ns::format( "Associating URLs with record {}", imported->record_id ), result.error() ) );

		std::vector< std::string > tags {};

		if ( const auto& declared { m_options[ "tags" ] }; declared.isArray() )
		{
			for ( const auto& tag : declared )
			{
				if ( tag.isString() ) tags.emplace_back( tag.asString() );
			}
		}

		if ( const auto result { co_await associateTags( imported->record_id, tags, m_tag_domain, db ) }; !result )
			co_return std::unexpected( downloaderResponseError(
				format_ns::format( "Applying tags to record {}", imported->record_id ), result.error() ) );

		std::string note {};

		if ( m_session_id != 0 )
		{
			const auto recorded { co_await db->execSqlCoro(
				"INSERT INTO download_session_records (download_session_id, record_id) VALUES ($1, $2) "
				"ON CONFLICT DO NOTHING RETURNING record_id",
				m_session_id,
				imported->record_id ) };

			const bool first_in_session { !recorded.empty() };

			if ( imported->status == ImportStatus::Exists )
				note = first_in_session ? "Already in the database" : "Already imported earlier in this session";
		}

		co_return ImportResult { .record_id = imported->record_id, .note = std::move( note ) };
	};

	auto outcome { drogon::sync_wait( completed() ) };

	if ( mapping != nullptr ) ::munmap( mapping, size );

	discard();
	return outcome;
}

class DownloadSessionManager::Secrets final : public SecretProvider
{
  public:

	std::optional< std::string > secret( const std::string_view name ) override
	{
		std::optional< std::string > value {};

		try
		{
			drogon::sync_wait(
				[ key = std::string { name }, &value ]() -> drogon::Task< void >
				{
					const auto rows { co_await drogon::app().getDbClient()->execSqlCoro(
						"SELECT value FROM downloader_secrets WHERE name = $1", key ) };

					if ( rows.size() > 0 ) value = rows[ 0 ][ "value" ].as< std::string >();
				}() );
		}
		catch ( const std::exception& e )
		{
			log::warn( "Unable to read downloader secret {}: {}", name, e.what() );
			return std::nullopt;
		}

		return value;
	}

	[[nodiscard]] drogon::Task< std::expected< std::unordered_map< std::string, std::string >, std::string > > values()
		const
	{
		std::unordered_map< std::string, std::string > values {};

		try
		{
			const auto rows {
				co_await drogon::app().getDbClient()->execSqlCoro( "SELECT name, value FROM downloader_secrets" )
			};

			for ( const auto& row : rows )
				values.emplace( row[ "name" ].as< std::string >(), row[ "value" ].as< std::string >() );
		}
		catch ( const std::exception& e )
		{
			co_return std::unexpected( format_ns::format( "Unable to read downloader secrets: {}", e.what() ) );
		}

		co_return values;
	}

	[[nodiscard]] drogon::Task< std::expected< void, std::string > > set(
		std::unordered_map< std::string, std::string > values )
	{
		std::vector< std::string > names {};
		std::vector< std::string > secrets {};
		names.reserve( values.size() );
		secrets.reserve( values.size() );

		for ( auto& [ name, value ] : values )
		{
			names.emplace_back( name );
			secrets.emplace_back( std::move( value ) );
		}

		try
		{
			co_await drogon::app().getDbClient()->execSqlCoro(
				"INSERT INTO downloader_secrets (name, value) SELECT * FROM UNNEST($1::text[], $2::text[]) "
				"ON CONFLICT (name) DO UPDATE SET value = EXCLUDED.value",
				std::move( names ),
				std::move( secrets ) );
		}
		catch ( const std::exception& e )
		{
			co_return std::unexpected( format_ns::format( "Unable to store downloader secrets: {}", e.what() ) );
		}

		co_return std::expected< void, std::string > {};
	}
};

DownloadSessionUrlID DownloadSessionManager::addRow(
	const DownloadSessionID session_id,
	const DownloadSessionUrlID parent_row,
	const std::string& url,
	const std::string_view state,
	std::string message )
{
	DownloadSessionUrlID row_id {};
	drogon::sync_wait(
		[ & ]() -> drogon::Task< void >
		{
			const auto inserted { co_await drogon::app().getDbClient()->execSqlCoro(
				"INSERT INTO download_session_urls "
				"(download_session_id, url, state, parent_url_id, note, error, finished_at) VALUES "
				"($1, $2, $3, $4, "
				"CASE WHEN $3 = 'failed' THEN NULL ELSE NULLIF($5, '') END, "
				"CASE WHEN $3 = 'failed' THEN COALESCE(NULLIF($5, ''), 'The download failed without a reason') END, "
				"CASE WHEN $3 IN ('pending', 'processing') THEN NULL ELSE now() END) "
				"RETURNING download_session_url_id",
				session_id,
				url,
				std::string { state },
				parent_row,
				message ) };
			row_id = inserted[ 0 ][ "download_session_url_id" ].as< DownloadSessionUrlID >();
		}() );
	DownloadSessionEventHub::instance().notify( session_id );
	return row_id;
}

void DownloadSessionManager::markRow(
	const DownloadSessionID session_id,
	const DownloadSessionUrlID row_id,
	const std::string_view state,
	std::string error )
{
	drogon::sync_wait(
		[ row_id, target = std::string { state }, error = std::move( error ) ]() -> drogon::Task< void >
		{
			co_await drogon::app().getDbClient()->execSqlCoro(
				"UPDATE download_session_urls SET state = $2, "
				"finished_at = CASE WHEN $2 IN ('pending', 'processing') THEN NULL ELSE now() END, "
				"error = CASE WHEN $2 = 'failed' "
				"THEN COALESCE(NULLIF($3, ''), 'The download failed without a reason') END "
				"WHERE download_session_url_id = $1",
				row_id,
				target,
				error );
		}() );
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
	drogon::sync_wait(
		[ session_id, row_id, &url, &lane, status, message = std::move( message ) ]() -> drogon::Task< void >
		{
			co_await drogon::app().getDbClient()->execSqlCoro(
				"INSERT INTO download_session_errors "
				"(download_session_id, download_session_url_id, url, lane, status, message) VALUES "
				"($1, NULLIF($2, 0::bigint), $3, $4, $5, NULLIF($6, ''))",
				session_id,
				row_id,
				url,
				lane,
				status,
				message );
		}() );
	DownloadSessionEventHub::instance().notify( session_id );
}

SessionRowObserver::SessionRowObserver( DownloadSessionManager& manager, const DownloadSessionID session_id ) :
  m_manager( manager ),
  m_session_id( session_id )
{}

void SessionRowObserver::adopt( const WorkID work, const DownloadSessionUrlID row )
{
	const std::scoped_lock lock { m_mutex };
	m_rows.insert_or_assign( work, row );
}

void SessionRowObserver::forget( const WorkID work )
{
	const std::scoped_lock lock { m_mutex };
	m_rows.erase( work );
}

DownloadSessionUrlID SessionRowObserver::rowFor( const WorkID work ) const
{
	const std::scoped_lock lock { m_mutex };
	const auto found { m_rows.find( work ) };
	return found == m_rows.end() ? DownloadSessionUrlID {} : found->second;
}

std::unordered_map< WorkID, DownloadSessionUrlID > SessionRowObserver::rows() const
{
	const std::scoped_lock lock { m_mutex };
	return m_rows;
}

void SessionRowObserver::onStarted( const WorkInfo& info )
{
	if ( const auto row { rowFor( info.id ) }; row != 0 )
		DownloadSessionManager::markRow( m_session_id, row, "processing", {} );
}

void SessionRowObserver::onCompleted( const WorkInfo& info )
{
	if ( const auto row { rowFor( info.id ) }; row != 0 )
		DownloadSessionManager::markRow( m_session_id, row, "completed", {} );
}

void SessionRowObserver::onFailed( const WorkInfo& info, const std::string& error )
{
	log::warn( "Downloader session {}: {} failed: {}", m_session_id, info.url, error );

	if ( const auto row { rowFor( info.id ) }; row != 0 )
		DownloadSessionManager::markRow( m_session_id, row, "failed", error );
}

void SessionRowObserver::onRequestFailed( const RequestFailure& failure )
{
	DownloadSessionManager::addError(
		m_session_id, rowFor( failure.work ), failure.url, failure.lane, failure.status, failure.message );
}

void SessionRowObserver::onImported( const ImportInfo& info )
{
	const auto parent { rowFor( info.work ) };
	const auto row { DownloadSessionManager::addRow( m_session_id, parent, info.url, "completed", info.note ) };
	const RecordID record { info.record_id };

	drogon::sync_wait(
		[ row, record ]() -> drogon::Task< void >
		{
			co_await drogon::app().getDbClient()->execSqlCoro(
				"UPDATE download_session_urls SET record_id = $2 WHERE download_session_url_id = $1", row, record );
		}() );
	DownloadSessionEventHub::instance().notify( m_session_id );
	log::info( "Downloader session {} imported {} as record {}", m_session_id, info.url, record );
}

void SessionRowObserver::onImportFailed( const WorkInfo& info, const std::string& url, const std::string& error )
{
	const auto parent { rowFor( info.id ) };
	(void)DownloadSessionManager::addRow( m_session_id, parent, url, "failed", error );
}

void SessionRowObserver::onFollowed( const WorkInfo& info, const FollowStatus status )
{
	const auto parent { rowFor( *info.parent ) };

	switch ( status )
	{
		case FollowStatus::QUEUED:
			{
				const auto row { DownloadSessionManager::addRow( m_session_id, parent, info.url, "pending", {} ) };
				adopt( info.id, row );
				break;
			}
		case FollowStatus::FILTERED:
			(void)DownloadSessionManager::addRow(
				m_session_id, parent, info.url, "skipped", "No URL class accepts this URL" );
			break;
		case FollowStatus::ALREADY_QUEUED:
		case FollowStatus::ALREADY_EXPLORED:
			(void)DownloadSessionManager::addRow(
				m_session_id, parent, info.url, "skipped", "Already seen in this session" );
			break;
		case FollowStatus::ALREADY_IMPORTED:
			(void)
				DownloadSessionManager::addRow( m_session_id, parent, info.url, "skipped", "Already in the database" );
			break;
	}
}

std::optional< std::int64_t > SessionRowObserver::alreadyImported( const std::string& )
{
	return std::nullopt;
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

	const std::string version { config::get< std::string >( "downloader", "http_version", "3" ) };

	if ( version == "1.1" )
		configuration.http_version = HttpVersion::HTTP_1_1;
	else if ( version == "2" )
		configuration.http_version = HttpVersion::HTTP_2;
	else
		configuration.http_version = HttpVersion::HTTP_3;

	for ( const auto& [ name, enabled ] : configuration.flags ) log::info( "Downloader flag {} = {}", name, enabled );

	m_tag_domain = config::get< std::string >( "downloader", "tag_domain", "default" );

	m_secrets = std::make_unique< Secrets >();
	m_imports = std::make_unique< Imports >( m_tag_domain );
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

drogon::Task< void > DownloadSessionManager::restore( const drogon::orm::DbClientPtr& db )
{
	if ( const auto initialized { initialize() }; !initialized )
	{
		log::error( "Unable to initialize downloader: {}", initialized.error() );
		co_return;
	}

	const auto roots { co_await db->execSqlCoro(
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
		"WHERE root.parent_url_id IS NULL ORDER BY root.download_session_url_id" ) };

	for ( const auto& row : roots )
	{
		const auto job_id { row[ "download_session_url_id" ].as< DownloadSessionUrlID >() };

		co_await db->execSqlCoro( "DELETE FROM download_session_urls WHERE parent_url_id = $1", job_id );
		co_await db->execSqlCoro(
			"UPDATE download_session_urls SET state = 'pending', finished_at = NULL, error = NULL "
			"WHERE download_session_url_id = $1",
			job_id );

		const auto scheduled {
			submit( job_id, row[ "download_session_id" ].as< DownloadSessionID >(), row[ "url" ].as< std::string >() )
		};

		if ( !scheduled )
			co_await db->execSqlCoro(
				"UPDATE download_session_urls SET state = 'failed', finished_at = now(), error = $2 "
				"WHERE download_session_url_id = $1",
				job_id,
				scheduled.error() );
	}
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

	sessions.clear();

	if ( downloader ) downloader->shutdown();

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

drogon::Task< std::expected< std::unordered_map< std::string, std::string >, std::string > > DownloadSessionManager::
	secrets()
{
	const auto initialized { initialize() };

	if ( !initialized ) co_return std::unexpected( initialized.error() );

	Secrets* secrets {};

	{
		const std::scoped_lock lock { m_mutex };

		if ( m_downloader == nullptr || m_secrets == nullptr )
			co_return std::unexpected( "The downloader is not running" );

		secrets = m_secrets.get();
	}

	co_return co_await secrets->values();
}

drogon::Task< std::expected< void, std::string > > DownloadSessionManager::setSecrets(
	std::unordered_map< std::string, std::string > values )
{
	const auto initialized { initialize() };

	if ( !initialized ) co_return std::unexpected( initialized.error() );

	Secrets* secrets {};

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
