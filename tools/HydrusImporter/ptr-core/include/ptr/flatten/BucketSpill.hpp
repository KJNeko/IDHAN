#pragma once

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <vector>

#include "ptr/flatten/MappingEvent.hpp"

namespace idhan::hydrus::ptr
{

//! Path of one bucket's spill file inside \p dir.
std::filesystem::path bucketPath( const std::filesystem::path& dir, std::size_t bucket );

//! Reads a whole bucket into memory. A bucket is sized to be comfortably RAM-resident.
//! A file that does not exist reads as empty -- a bucket no event routed to is legitimate.
std::vector< MappingEvent > readBucket( const std::filesystem::path& path );

//! Fans MappingEvents out to BUCKET_COUNT append-only files, buffering each separately.
//!
//! The buffers are the dominant memory cost of the scan stage: BUCKET_COUNT * buffer_events *
//! sizeof(MappingEvent). The default of 5461 events per bucket is roughly 64 KB each, about
//! 256 MB in total. Lower it if that is too much.
class BucketWriter
{
  public:

	static constexpr std::size_t DEFAULT_BUFFER_EVENTS { 5461 };

	explicit BucketWriter( std::filesystem::path dir, std::size_t buffer_events = DEFAULT_BUFFER_EVENTS );

	BucketWriter( const BucketWriter& ) = delete;
	BucketWriter& operator=( const BucketWriter& ) = delete;
	BucketWriter( BucketWriter&& ) = delete;
	BucketWriter& operator=( BucketWriter&& ) = delete;

	//! Flushes every buffer. Errors are logged, not thrown -- a destructor must not throw.
	~BucketWriter();

	void write( const MappingEvent& event );

	//! Writes out every buffer; call before reading any bucket back.
	void flush();

	std::uint64_t written() const noexcept { return m_written; }

  private:

	struct FileCloser
	{
		void operator()( std::FILE* file ) const noexcept;
	};

	using FilePtr = std::unique_ptr< std::FILE, FileCloser >;

	void flushBucket( std::size_t bucket );

	std::filesystem::path m_dir;
	std::size_t m_buffer_events;
	std::vector< FilePtr > m_files {};
	std::vector< std::vector< MappingEvent > > m_buffers {};
	std::uint64_t m_written { 0 };
};

} // namespace idhan::hydrus::ptr
