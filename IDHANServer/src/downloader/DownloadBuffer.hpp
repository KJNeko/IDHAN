#pragma once

#include <folly/Executor.h>
#include <folly/File.h>

#include <expected>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "filesystem/io/IOUring.hpp"

namespace idhan::downloader
{

class DownloadBuffer
{
	IOUring& m_io;
	folly::File m_file {};
	folly::Executor::KeepAlive<> m_executor {};
	std::size_t m_size {};
	std::string m_error {};
	bool m_discarded {};

	runtime::Task< std::expected< void, std::string > > open();
	static runtime::Task< int > closeFile( IOUring& io, folly::File file );
	static void reportClose( folly::Try< int >&& result );

  public:

	//! Calls must be serialized. The backend must outlive queued close tasks.
	explicit DownloadBuffer( IOUring& io = IOUring::getInstance() );
	~DownloadBuffer();

	DownloadBuffer( const DownloadBuffer& ) = delete;
	DownloadBuffer& operator=( const DownloadBuffer& ) = delete;

	//! Borrowed until scheduleDiscard() or discard().
	[[nodiscard]] std::optional< int > descriptor() const;

	[[nodiscard]] runtime::Task< std::expected< void, std::string > > write( std::span< const std::byte > bytes );
	[[nodiscard]] runtime::Task< std::expected< std::vector< std::byte >, std::string > > read( std::size_t size );

	//! Queues cleanup; discard() waits for cleanup instead.
	void scheduleDiscard() noexcept;
	[[nodiscard]] runtime::Task< void > discard();
};

} // namespace idhan::downloader
