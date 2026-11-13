#pragma once

#include <IDHANDownloader/DownloaderContext.hpp>
#include <IDHANDownloader/imports/ImportSink.hpp>
#include <IDHANDownloader/secrets/SecretProvider.hpp>
#include <IDHANDownloader/session/SessionObserver.hpp>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <map>
#include <random>
#include <string>
#include <vector>

namespace idhan::test
{

class ParserFixture
{
	std::filesystem::path m_root {};

  public:

	ParserFixture()
	{
		std::random_device device {};
		m_root = std::filesystem::temp_directory_path() / std::format( "idhan-downloader-test-{}", device() );
		std::filesystem::create_directories( m_root );
		// Router-only tests still bind to a real module, even when no work is executed.
		write( "test.js", "export function page() {}" );
	}

	ParserFixture( const ParserFixture& ) = delete;
	ParserFixture& operator=( const ParserFixture& ) = delete;

	~ParserFixture()
	{
		std::error_code error {};
		std::filesystem::remove_all( m_root, error );
	}

	[[nodiscard]] const std::filesystem::path& root() const { return m_root; }

	[[nodiscard]] std::filesystem::path urlClassesPath() const { return m_root / "url-classes.json"; }

	void write( const std::string_view name, const std::string_view contents ) const
	{
		std::ofstream output { m_root / name, std::ios::binary | std::ios::trunc };
		output << contents;
	}
};

struct RecordedImport
{
	downloader::WorkID work {};
	std::string url {};
	std::int64_t record_id {};
	std::uint64_t size {};
	std::string content_type {};
};

class RecordingObserver final : public downloader::SessionObserver
{
	mutable std::mutex m_mutex {};

  public:

	std::vector< downloader::WorkInfo > started {};
	std::vector< downloader::WorkInfo > completed {};
	std::vector< downloader::RequestInfo > requests {};
	std::vector< RecordedImport > imports {};
	std::vector< std::pair< std::string, downloader::FollowStatus > > follows {};
	std::vector< std::pair< std::string, std::string > > url_types {};
	std::optional< std::string > url_type_error {};
	std::vector< std::pair< downloader::WorkID, std::string > > failures {};
	std::vector< std::pair< std::string, std::string > > import_failures {};
	std::vector< downloader::FailureStage > import_failure_stages {};
	std::atomic_size_t finished {};

	folly::coro::Task< void > onStarted( downloader::WorkInfo info ) override
	{
		const std::scoped_lock lock { m_mutex };
		started.emplace_back( info );

		co_return;
	}

	folly::coro::Task< void > onRequest( downloader::RequestInfo info ) override
	{
		const std::scoped_lock lock { m_mutex };
		requests.emplace_back( info );

		co_return;
	}

	folly::coro::Task< void > onImported( downloader::ImportInfo info ) override
	{
		const std::scoped_lock lock { m_mutex };
		imports.emplace_back(
			RecordedImport {
				.work = info.work,
				.url = info.url,
				.record_id = info.record_id,
				.size = info.size,
				.content_type = info.content_type } );

		co_return;
	}

	folly::coro::Task< void > onCompleted( downloader::WorkInfo info ) override
	{
		const std::scoped_lock lock { m_mutex };
		completed.emplace_back( info );

		co_return;
	}

	folly::coro::Task< void > onFollowed( downloader::WorkInfo info, const downloader::FollowStatus status ) override
	{
		const std::scoped_lock lock { m_mutex };
		follows.emplace_back( info.url, status );

		co_return;
	}

	folly::coro::Task< std::expected< void, std::string > > onUrlTypeSet( std::string url, std::string type ) override
	{
		const std::scoped_lock lock { m_mutex };
		if ( url_type_error ) co_return std::unexpected( *url_type_error );

		url_types.emplace_back( url, type );
		co_return std::expected< void, std::string > {};
	}

	folly::coro::Task< void > onFailed( downloader::WorkInfo info, std::string error ) override
	{
		const std::scoped_lock lock { m_mutex };
		failures.emplace_back( info.id, error );

		co_return;
	}

	folly::coro::Task< void > onImportFailed( downloader::WorkInfo, std::string url, std::string error ) override
	{
		const std::scoped_lock lock { m_mutex };
		import_failures.emplace_back( url, error );

		co_return;
	}

	folly::coro::Task< void > onImportFailure(
		downloader::WorkInfo info,
		std::string url,
		downloader::WorkFailure failure ) override
	{
		{
			const std::scoped_lock lock { m_mutex };
			import_failure_stages.push_back( failure.stage );
		}
		co_await onImportFailed( info, url, failure.message );
		co_return;
	}

	folly::coro::Task< void > onFinished() override
	{
		finished.fetch_add( 1 );
		co_return;
	}

	[[nodiscard]] bool hasCompleted( const downloader::WorkID work ) const
	{
		const std::scoped_lock lock { m_mutex };
		return std::ranges::any_of(
			completed, [ work ]( const downloader::WorkInfo& info ) { return info.id == work; } );
	}
};

class RecordingLaneObserver final : public downloader::LaneObserver
{
	mutable std::mutex m_mutex {};
	std::vector< downloader::LaneSnapshot > m_snapshots {};

  public:

	void onLaneChanged( const downloader::LaneSnapshot& snapshot ) override
	{
		const std::scoped_lock lock { m_mutex };
		m_snapshots.emplace_back( snapshot );
	}

	[[nodiscard]] std::vector< downloader::LaneSnapshot > snapshots() const
	{
		const std::scoped_lock lock { m_mutex };
		return m_snapshots;
	}
};

class MemoryImportFactory final : public downloader::ImportSinkFactory
{
	class Sink final : public downloader::ImportSink
	{
		MemoryImportFactory& m_owner;
		std::string m_bytes {};
		downloader::ImportRequest m_request {};

	  public:

		Sink( MemoryImportFactory& owner, downloader::ImportRequest request ) :
		  m_owner( owner ),
		  m_request( std::move( request ) )
		{}

		folly::coro::Task< std::expected< void, std::string > > write( std::span< const std::byte > bytes ) override
		{
			m_owner.async_writes.fetch_add( 1 );
			m_bytes.append( reinterpret_cast< const char* >( bytes.data() ), bytes.size() );
			co_return std::expected< void, std::string > {};
		}

		folly::coro::Task< std::expected< downloader::ImportResult, std::string > > finish(
			downloader::ImportMetadata metadata ) override
		{
			const std::scoped_lock lock { m_owner.m_mutex };
			if ( m_owner.finish_error.has_value() ) co_return std::unexpected( m_owner.finish_error.value() );

			const auto failure { m_owner.finish_errors.find( m_request.url ) };
			if ( failure != m_owner.finish_errors.end() ) co_return std::unexpected( failure->second );

			m_owner.finished_requests.emplace_back( m_request );
			m_owner.metadata.emplace_back( metadata );
			if ( m_request.stored_record.has_value() )
			{
				co_return downloader::ImportResult {
					.record_id = m_request.stored_record.value(), .note = "Already in the database", .existing = true
				};
			}

			const RecordID id { m_owner.m_next++ };
			m_owner.stored.emplace_back( id, std::move( m_bytes ) );
			co_return downloader::ImportResult { .record_id = id };
		}

		void abort() override { m_owner.aborted.fetch_add( 1 ); }
	};

	mutable std::mutex m_mutex {};
	RecordID m_next { 1 };

  public:

	std::vector< std::pair< RecordID, std::string > > stored {};
	std::vector< downloader::ImportMetadata > metadata {};
	std::atomic_size_t aborted {};
	std::atomic_size_t async_writes {};
	std::optional< RecordID > stored_record {};
	std::optional< std::string > finish_error {};
	std::map< std::string, std::string > finish_errors {};
	std::vector< downloader::ImportRequest > opened_requests {};
	std::vector< downloader::ImportRequest > finished_requests {};
	std::optional< std::string > lookup_error {};
	std::vector< downloader::ImportRequest > lookup_requests {};

	folly::coro::Task< std::expected< std::optional< RecordID >, std::string > > findStoredRecord(
		downloader::ImportRequest request ) override
	{
		const std::scoped_lock lock { m_mutex };
		lookup_requests.emplace_back( request );
		if ( lookup_error ) co_return std::unexpected( *lookup_error );

		co_return stored_record;
	}

	std::expected< std::unique_ptr< downloader::ImportSink >, std::string > open(
		const downloader::ImportRequest& request ) override
	{
		const std::scoped_lock lock { m_mutex };
		opened_requests.emplace_back( request );
		return std::make_unique< Sink >( *this, request );
	}
};

class MapSecrets final : public downloader::SecretProvider
{
  public:

	std::unordered_map< std::string, Json::Value > values {};

	std::expected< Json::Value, std::string > secrets( const std::string_view domain ) override
	{
		const auto found { values.find( std::string { domain } ) };

		return found == values.end() ? Json::Value { Json::objectValue } : found->second;
	}
};

} // namespace idhan::test
