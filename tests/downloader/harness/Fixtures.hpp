#pragma once

#include <IDHANDownloader/DownloaderContext.hpp>
#include <IDHANDownloader/ImportSink.hpp>
#include <IDHANDownloader/SecretProvider.hpp>
#include <IDHANDownloader/SessionObserver.hpp>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <mutex>
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
	std::vector< std::pair< downloader::WorkID, std::string > > failures {};
	std::vector< std::pair< std::string, std::string > > import_failures {};
	std::atomic_size_t finished {};

	void onStarted( const downloader::WorkInfo& info ) override
	{
		const std::scoped_lock lock { m_mutex };
		started.emplace_back( info );
	}

	void onRequest( const downloader::RequestInfo& info ) override
	{
		const std::scoped_lock lock { m_mutex };
		requests.emplace_back( info );
	}

	void onImported( const downloader::ImportInfo& info ) override
	{
		const std::scoped_lock lock { m_mutex };
		imports.emplace_back(
			RecordedImport {
				.work = info.work,
				.url = info.url,
				.record_id = info.record_id,
				.size = info.size,
				.content_type = info.content_type } );
	}

	void onCompleted( const downloader::WorkInfo& info ) override
	{
		const std::scoped_lock lock { m_mutex };
		completed.emplace_back( info );
	}

	void onFollowed( const downloader::WorkInfo& info, const downloader::FollowStatus status ) override
	{
		const std::scoped_lock lock { m_mutex };
		follows.emplace_back( info.url, status );
	}

	void onFailed( const downloader::WorkInfo& info, const std::string& error ) override
	{
		const std::scoped_lock lock { m_mutex };
		failures.emplace_back( info.id, error );
	}

	void onImportFailed( const downloader::WorkInfo&, const std::string& url, const std::string& error ) override
	{
		const std::scoped_lock lock { m_mutex };
		import_failures.emplace_back( url, error );
	}

	void onFinished() override { finished.fetch_add( 1 ); }

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

	  public:

		explicit Sink( MemoryImportFactory& owner ) : m_owner( owner ) {}

		std::expected< void, std::string > write( const std::span< const std::byte > bytes ) override
		{
			m_bytes.append( reinterpret_cast< const char* >( bytes.data() ), bytes.size() );
			return {};
		}

		std::expected< downloader::ImportResult, std::string > finish( const downloader::ImportMetadata& metadata )
			override
		{
			const std::scoped_lock lock { m_owner.m_mutex };
			const std::int64_t id { m_owner.m_next++ };
			m_owner.stored.emplace_back( id, std::move( m_bytes ) );
			m_owner.metadata.emplace_back( metadata );
			return downloader::ImportResult { .record_id = id };
		}

		void abort() override { m_owner.aborted.fetch_add( 1 ); }
	};

	mutable std::mutex m_mutex {};
	std::int64_t m_next { 1 };

  public:

	std::vector< std::pair< std::int64_t, std::string > > stored {};
	std::vector< downloader::ImportMetadata > metadata {};
	std::atomic_size_t aborted {};

	std::expected< std::unique_ptr< downloader::ImportSink >, std::string > open( const downloader::ImportRequest& )
		override
	{
		return std::make_unique< Sink >( *this );
	}
};

class MapSecrets final : public downloader::SecretProvider
{
  public:

	std::unordered_map< std::string, std::string > values {};

	std::optional< std::string > secret( const std::string_view name ) override
	{
		const auto found { values.find( std::string { name } ) };

		return found == values.end() ? std::nullopt : std::optional { found->second };
	}
};

} // namespace idhan::test
