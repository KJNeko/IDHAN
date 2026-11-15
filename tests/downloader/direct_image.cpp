#include <IDHANDownloader/DownloaderContext.hpp>
#include <folly/coro/BlockingWait.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <span>
#include <vector>

namespace idhan::downloader::tests
{

class ImportObserver
{
  public:

	std::vector< std::byte > bytes {};
	MOCK_METHOD( void, opened, ( const std::string& url ) );
	MOCK_METHOD( void, finished, ( const ImportMetadata& metadata ) );
	MOCK_METHOD( void, aborted, () );
};

class MemorySink final : public ImportSink
{
	ImportObserver& m_observer;

  public:

	explicit MemorySink( ImportObserver& observer ) : m_observer( observer ) {}

	folly::coro::Task< std::expected< void, std::string > > write( std::span< const std::byte > bytes ) override
	{
		m_observer.bytes.insert( m_observer.bytes.end(), bytes.begin(), bytes.end() );
		co_return std::expected< void, std::string > {};
	}

	folly::coro::Task< std::expected< ImportResult, std::string > > finish( ImportMetadata metadata ) override
	{
		m_observer.finished( metadata );
		co_return ImportResult { .record_id = RecordID { 1 } };
	}

	void abort() override { m_observer.aborted(); }
};

class MemoryImports final : public ImportSinkFactory
{
	ImportObserver& m_observer;

  public:

	explicit MemoryImports( ImportObserver& observer ) : m_observer( observer ) {}

	std::expected< std::unique_ptr< ImportSink >, std::string > open( const ImportRequest& request ) override
	{
		m_observer.opened( request.url );
		return std::make_unique< MemorySink >( m_observer );
	}
};

TEST( Downloader, ImportsDirectImage )
{
	const char* fixture_directory { std::getenv( "IDHAN_DOWNLOADER_FIXTURES" ) };
	ASSERT_NE( fixture_directory, nullptr ) << "Run through run_tests.py or CTest";
	std::ifstream image { std::filesystem::path { fixture_directory } / "4.png", std::ios::binary };
	ASSERT_TRUE( image.is_open() );
	const std::vector< char > expected {
		std::istreambuf_iterator< char > { image }, std::istreambuf_iterator< char > {}
	};
	ASSERT_FALSE( expected.empty() );

	testing::StrictMock< ImportObserver > observer {};
	MemoryImports imports { observer };
	const std::string url { "http://example.com/4.png" };
	EXPECT_CALL( observer, opened( url ) ).Times( 1 );
	EXPECT_CALL(
		observer,
		finished(
			testing::AllOf(
				testing::Field( &ImportMetadata::size, expected.size() ),
				testing::Field( &ImportMetadata::content_type, "image/png" ),
				testing::Field( &ImportMetadata::filename, "4.png" ),
				testing::Field( &ImportMetadata::effective_url, url ) ) ) )
		.Times( 1 );
	EXPECT_CALL( observer, aborted() ).Times( 0 );

	DownloaderConfig config {};
	config.parser_directory = fixture_directory;
	config.watch_scripts = false;
	config.io_threads = 1;
	config.import_threads = 1;
	config.http_version = HttpVersion::HTTP_1_1;
	auto context_e {
		folly::coro::blockingWait( DownloaderContext::create( config, DownloaderHost { .imports = &imports } ) )
	};
	ASSERT_TRUE( context_e.has_value() ) << context_e.error();
	auto& context { context_e.value() };
	auto session { context->createSession( SessionOptions { .root_url = url } ) };
	const auto submitted_e { session->submit( url ) };
	ASSERT_TRUE( submitted_e.has_value() ) << submitted_e.error();
	folly::coro::blockingWait( session->wait() );
	session->close();
	folly::coro::blockingWait( context->shutdown() );

	const auto expected_bytes { std::as_bytes( std::span { expected } ) };
	EXPECT_THAT( observer.bytes, testing::ElementsAreArray( expected_bytes ) );
	const auto counters { session->snapshot().counters };
	EXPECT_EQ( counters.imported, 1 );
	EXPECT_EQ( counters.import_failed, 0 );
}

} // namespace idhan::downloader::tests
