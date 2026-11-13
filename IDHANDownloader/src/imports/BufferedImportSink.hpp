#pragma once

#include <folly/Executor.h>
#include <folly/coro/Baton.h>

#include <deque>
#include <memory>
#include <mutex>
#include <vector>

#include "IDHANDownloader/imports/ImportSink.hpp"

namespace idhan::downloader
{
//! Owns queued chunks until a writer consumes them. Curl pauses when the buffer is full.
class BufferedImportSink final
{
	struct State
	{
		std::mutex mutex {};
		folly::coro::Baton drained {};
		std::unique_ptr< ImportSink > sink {};
		std::deque< std::vector< std::byte > > chunks {};
		std::size_t bytes {};
		bool running {};
		bool waiting {};
		bool aborted {};
		std::string error {};
		std::function< void() > resume {};
	};

	std::shared_ptr< State > m_state;
	folly::Executor& m_executor;
	folly::coro::Task< std::expected< void, std::string > > waitForDrain();
	static folly::coro::Task< void > drain( std::shared_ptr< State > state );
	static folly::coro::Task< std::expected< void, std::string > > writeChunk(
		ImportSink& sink,
		std::span< const std::byte > bytes );

  public:

	BufferedImportSink( std::unique_ptr< ImportSink > sink, folly::Executor& executor );
	bool canWrite( std::size_t bytes ) const;
	void setResumeCallback( std::function< void() > callback );
	std::expected< void, std::string > enqueue( std::span< const std::byte > bytes );
	folly::coro::Task< std::expected< void, std::string > > write( std::span< const std::byte > bytes );
	folly::coro::Task< std::expected< ImportResult, std::string > > finish( ImportMetadata metadata );
	void abort();
};
} // namespace idhan::downloader
