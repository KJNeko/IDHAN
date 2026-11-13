#include <folly/coro/Baton.h>
#include <folly/coro/BlockingWait.h>
#include <folly/executors/CPUThreadPoolExecutor.h>

#include <atomic>
#include <chrono>
#include <fcntl.h>
#include <future>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <unistd.h>

#include "imports/BufferedImportSink.hpp"
#include "downloader/DownloadBuffer.hpp"

// Include after Folly's glog headers so CHECK remains Catch2's assertion macro.
#include <catch2/catch_test_macros.hpp>

namespace idhan::test
{

class DownloadBufferIO final : public IOUring
{
  public:

	std::vector< std::byte > bytes {};
	std::vector< std::size_t > offsets {};
	std::vector< int > open_flags {};
	int open_error {};
	int remove_error {};
	int close_error {};
	bool unnamed_supported { true };
	bool fail_write {};
	bool short_read {};
	bool pause_write {};
	int removes {};
	int reads {};
	std::atomic_int closes {};
	std::promise< void > write_started {};
	folly::coro::Baton resume_write {};
	std::promise< void > closed {};

	runtime::Task< int > openFile( std::filesystem::path, int flags, unsigned int ) override
	{
		open_flags.push_back( flags );
		if ( open_error != 0 ) co_return -open_error;

		if ( !unnamed_supported && ( flags & O_TMPFILE ) == O_TMPFILE ) co_return -EOPNOTSUPP;

		co_return ::open( "/dev/null", O_RDONLY | O_CLOEXEC );
	}

	runtime::Task< void > write( NativeHandle, std::span< const std::byte > data, std::size_t offset ) override
	{
		if ( pause_write )
		{
			write_started.set_value();
			co_await resume_write;
		}

		offsets.push_back( offset );
		if ( fail_write )
		{
			bytes.push_back( data.front() );
			throw std::runtime_error( "injected write failure after partial progress" );
		}

		bytes.insert( bytes.end(), data.begin(), data.end() );
		co_return;
	}

	runtime::Task< std::vector< std::byte > > read( NativeHandle, std::size_t offset, std::size_t size ) override
	{
		++reads;
		if ( short_read ) --size;

		const auto requested { std::span { bytes }.subspan( offset, size ) };
		co_return std::vector< std::byte > { requested.begin(), requested.end() };
	}

	runtime::Task< int > closeFile( NativeHandle handle ) override
	{
		::close( static_cast< int >( handle ) );
		closes.fetch_add( 1 );
		closed.set_value();
		co_return -close_error;
	}

	runtime::Task< int > removeFile( std::filesystem::path ) override
	{
		++removes;
		co_return -remove_error;
	}

	runtime::Task< int > syncFile( NativeHandle ) override { co_return -ENOSYS; }

	runtime::Task< int > renameFile( std::filesystem::path, std::filesystem::path ) override { co_return -ENOSYS; }

	runtime::Task< int > createDirectories( std::filesystem::path ) override { co_return -ENOSYS; }

	runtime::Task< std::expected< std::filesystem::file_status, int > > fileStatus( std::filesystem::path ) override
	{
		co_return std::unexpected( -ENOSYS );
	}

	runtime::Task< std::expected< std::uint64_t, int > > fileSize( std::filesystem::path ) override
	{
		throw std::runtime_error( "DownloadBuffer must use its known size instead of statting" );
		co_return std::unexpected( -ENOSYS );
	}
};

TEST_CASE( "Download buffers await writes and validate size without flushing or statting", "[downloader][buffer]" )
{
	DownloadBufferIO io {};
	folly::CPUThreadPoolExecutor executor { 1 };
	downloader::DownloadBuffer buffer { io };
	const std::string_view first { "downloaded" };
	const std::string_view second { " bytes" };
	REQUIRE(
		folly::coro::blockingWait(
			folly::coro::co_withExecutor( &executor, buffer.write( std::as_bytes( std::span { first } ) ) ) )
			.has_value() );
	REQUIRE(
		folly::coro::blockingWait(
			folly::coro::co_withExecutor( &executor, buffer.write( std::as_bytes( std::span { second } ) ) ) )
			.has_value() );
	CHECK( ( io.offsets == std::vector< std::size_t > { 0, first.size() } ) );
	CHECK( buffer.descriptor().has_value() );
	CHECK_FALSE(
		folly::coro::blockingWait(
			folly::coro::co_withExecutor( &executor, buffer.read( first.size() + second.size() + 1 ) ) )
			.has_value() );
	CHECK( io.reads == 0 );

	auto bytes_e { folly::coro::blockingWait(
		folly::coro::co_withExecutor( &executor, buffer.read( first.size() + second.size() ) ) ) };
	REQUIRE( bytes_e.has_value() );
	const auto& bytes { bytes_e.value() };
	CHECK(
		( std::string_view { reinterpret_cast< const char* >( bytes.data() ), bytes.size() } == "downloaded bytes" ) );
	folly::coro::blockingWait( folly::coro::co_withExecutor( &executor, buffer.discard() ) );
	CHECK( io.closes.load() == 1 );
	CHECK_FALSE( buffer.descriptor().has_value() );
	CHECK_FALSE(
		folly::coro::blockingWait( folly::coro::co_withExecutor( &executor, buffer.read( bytes.size() ) ) )
			.has_value() );
	CHECK_FALSE(
		folly::coro::blockingWait(
			folly::coro::co_withExecutor( &executor, buffer.write( std::as_bytes( std::span { first } ) ) ) )
			.has_value() );
}

TEST_CASE( "Download buffers reject partial writes and short reads", "[downloader][buffer]" )
{
	DownloadBufferIO io {};
	folly::CPUThreadPoolExecutor executor { 1 };
	downloader::DownloadBuffer buffer { io };
	const std::string_view payload { "payload" };
	SECTION( "A failed write poisons finalization even when only part was written" )
	{
		io.fail_write = true;
		CHECK_FALSE(
			folly::coro::blockingWait(
				folly::coro::co_withExecutor( &executor, buffer.write( std::as_bytes( std::span { payload } ) ) ) )
				.has_value() );
		CHECK_FALSE(
			folly::coro::blockingWait( folly::coro::co_withExecutor( &executor, buffer.read( 0 ) ) ).has_value() );
		CHECK( io.reads == 0 );
	}
	SECTION( "A short read is rejected before importing" )
	{
		REQUIRE(
			folly::coro::blockingWait(
				folly::coro::co_withExecutor( &executor, buffer.write( std::as_bytes( std::span { payload } ) ) ) )
				.has_value() );
		io.short_read = true;
		CHECK_FALSE(
			folly::coro::blockingWait( folly::coro::co_withExecutor( &executor, buffer.read( payload.size() ) ) )
				.has_value() );
	}
	folly::coro::blockingWait( folly::coro::co_withExecutor( &executor, buffer.discard() ) );
	CHECK( io.closes.load() == 1 );
}

TEST_CASE( "Download buffers create and unlink named temporaries through the backend", "[downloader][buffer]" )
{
	DownloadBufferIO io {};
	folly::CPUThreadPoolExecutor executor { 1 };
	downloader::DownloadBuffer buffer { io };
	io.unnamed_supported = false;
	SECTION( "Named creation succeeds" )
	{}
	SECTION( "Unlink failure closes the descriptor" )
	{
		io.remove_error = EACCES;
	}
	SECTION( "Open errors are preserved" )
	{
		io.open_error = ENOSPC;
	}
	const std::string_view payload { "bytes" };
	const auto written_e { folly::coro::blockingWait(
		folly::coro::co_withExecutor( &executor, buffer.write( std::as_bytes( std::span { payload } ) ) ) ) };
	if ( io.open_error != 0 )
	{
		CHECK_FALSE( written_e.has_value() );
		CHECK( io.open_flags.size() == 1 );
		CHECK( io.closes.load() == 0 );
		return;
	}

	REQUIRE( io.open_flags.size() == 2 );
	CHECK( ( io.open_flags[ 1 ] & ( O_CREAT | O_EXCL | O_CLOEXEC ) ) == ( O_CREAT | O_EXCL | O_CLOEXEC ) );
	CHECK( io.removes == 1 );
	CHECK( written_e.has_value() == ( io.remove_error == 0 ) );
	folly::coro::blockingWait( folly::coro::co_withExecutor( &executor, buffer.discard() ) );
	CHECK( io.closes.load() == 1 );
}

TEST_CASE( "Download buffer reads handle empty and multi-block files", "[downloader][buffer]" )
{
	DownloadBufferIO io {};
	folly::CPUThreadPoolExecutor executor { 1 };
	downloader::DownloadBuffer buffer { io };
	SECTION( "Empty downloads need no temporary file" )
	{
		const auto bytes_e { folly::coro::blockingWait( folly::coro::co_withExecutor( &executor, buffer.read( 0 ) ) ) };
		REQUIRE( bytes_e.has_value() );
		CHECK( bytes_e.value().empty() );
		CHECK( io.open_flags.empty() );
	}
	SECTION( "Reads span multiple backend operations without losing boundary bytes" )
	{
		std::vector< std::byte > payload( 9 * 1024 * 1024, std::byte { 0x42 } );
		payload[ 8 * 1024 * 1024 ] = std::byte { 0x7f };
		payload.back() = std::byte { 0xff };
		REQUIRE(
			folly::coro::blockingWait( folly::coro::co_withExecutor( &executor, buffer.write( payload ) ) )
				.has_value() );
		const auto bytes_e {
			folly::coro::blockingWait( folly::coro::co_withExecutor( &executor, buffer.read( payload.size() ) ) )
		};
		REQUIRE( bytes_e.has_value() );
		CHECK( bytes_e.value() == payload );
		CHECK( io.reads == 2 );
	}
	folly::coro::blockingWait( folly::coro::co_withExecutor( &executor, buffer.discard() ) );
}

TEST_CASE( "Download buffer close failures are reported without double closing", "[downloader][buffer]" )
{
	DownloadBufferIO io {};
	folly::CPUThreadPoolExecutor executor { 1 };
	downloader::DownloadBuffer buffer { io };
	const std::string_view payload { "bytes" };
	REQUIRE(
		folly::coro::blockingWait(
			folly::coro::co_withExecutor( &executor, buffer.write( std::as_bytes( std::span { payload } ) ) ) )
			.has_value() );
	io.close_error = EIO;
	CHECK_THROWS_AS(
		folly::coro::blockingWait( folly::coro::co_withExecutor( &executor, buffer.discard() ) ), std::system_error );
	CHECK_FALSE( buffer.descriptor().has_value() );
	folly::coro::blockingWait( folly::coro::co_withExecutor( &executor, buffer.discard() ) );
	CHECK( io.closes.load() == 1 );
}

class DownloadBufferSink final : public downloader::ImportSink
{
	downloader::DownloadBuffer m_buffer;

  public:

	explicit DownloadBufferSink( IOUring& io ) : m_buffer( io ) {}

	folly::coro::Task< std::expected< void, std::string > > write( std::span< const std::byte > bytes ) override
	{
		co_return co_await m_buffer.write( bytes );
	}

	folly::coro::Task< std::expected< downloader::ImportResult, std::string > > finish(
		downloader::ImportMetadata metadata ) override
	{
		auto bytes_e { co_await m_buffer.read( metadata.size ) };
		co_await m_buffer.discard();
		if ( !bytes_e.has_value() ) co_return std::unexpected( bytes_e.error() );

		co_return downloader::ImportResult { .record_id = 1 };
	}

	void abort() override { m_buffer.scheduleDiscard(); }
};

TEST_CASE( "Buffered imports suspend on disk writes without occupying their executor", "[downloader][buffer]" )
{
	DownloadBufferIO io {};
	folly::CPUThreadPoolExecutor executor { 1 };
	downloader::BufferedImportSink sink { std::make_unique< DownloadBufferSink >( io ), executor };
	io.pause_write = true;
	auto started { io.write_started.get_future() };
	auto closed { io.closed.get_future() };
	const std::string_view payload { "bytes retained across suspension" };
	REQUIRE( sink.enqueue( std::as_bytes( std::span { payload } ) ).has_value() );
	const auto started_status { started.wait_for( std::chrono::seconds { 5 } ) };
	// Release the gate on every assertion path, including failure, so executor teardown cannot hang.
	if ( started_status != std::future_status::ready ) io.resume_write.post();

	REQUIRE( started_status == std::future_status::ready );
	std::promise< void > responsive {};
	auto response { responsive.get_future() };
	executor.add( [ &responsive ] { responsive.set_value(); } );
	const auto response_status { response.wait_for( std::chrono::seconds { 5 } ) };

	SECTION( "Finish waits for the write" )
	{
		io.resume_write.post();
		const auto result_e { folly::coro::blockingWait(
			folly::coro::co_withExecutor( &executor, sink.finish( { .size = payload.size() } ) ) ) };
		REQUIRE( result_e.has_value() );
		CHECK(
			io.bytes
		    == std::vector< std::byte >(
				std::as_bytes( std::span { payload } ).begin(), std::as_bytes( std::span { payload } ).end() ) );
	}
	SECTION( "Abort waits for the in-flight write before closing" )
	{
		sink.abort();
		CHECK( io.closes.load() == 0 );
		io.resume_write.post();
		REQUIRE( closed.wait_for( std::chrono::seconds { 5 } ) == std::future_status::ready );
		const auto result_e { folly::coro::blockingWait(
			folly::coro::co_withExecutor( &executor, sink.finish( { .size = payload.size() } ) ) ) };
		CHECK_FALSE( result_e.has_value() );
	}
	CHECK( response_status == std::future_status::ready );
	CHECK( io.closes.load() == 1 );
}

TEST_CASE( "Async buffered writes complete only after the backend write", "[downloader][buffer]" )
{
	DownloadBufferIO io {};
	folly::CPUThreadPoolExecutor executor { 1 };
	downloader::BufferedImportSink sink { std::make_unique< DownloadBufferSink >( io ), executor };
	io.pause_write = true;
	const std::string_view payload { "pending disk write" };
	auto started { io.write_started.get_future() };
	auto written {
		folly::coro::co_withExecutor( &executor, sink.write( std::as_bytes( std::span { payload } ) ) ).start()
	};
	const auto status { started.wait_for( std::chrono::seconds { 5 } ) };
	if ( status != std::future_status::ready ) io.resume_write.post();

	REQUIRE( status == std::future_status::ready );
	CHECK_FALSE( written.isReady() );
	io.resume_write.post();
	const auto written_e { std::move( written ).get() };
	REQUIRE( written_e.has_value() );
	const auto result_e { folly::coro::blockingWait(
		folly::coro::co_withExecutor( &executor, sink.finish( { .size = payload.size() } ) ) ) };
	REQUIRE( result_e.has_value() );
	CHECK( io.closes.load() == 1 );
}

} // namespace idhan::test
