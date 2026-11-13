#pragma once

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <mutex>
#include <vector>

#include "ptr/flatten/MappingEvent.hpp"

namespace idhan::hydrus::ptr
{

//! Path of one bucket's spill file inside \p dir.
std::filesystem::path bucketPath( const std::filesystem::path& dir, std::size_t bucket );

//! Reads a whole bucket into memory. A bucket is sized to be comfortably RAM-resident.
//! A file that does not exist reads as empty, since a bucket no event routed to is legitimate.
std::vector< MappingEvent > readBucket( const std::filesystem::path& path );

//! Fans MappingEvents out to BUCKET_COUNT append-only files, buffering each separately.
//! Memory cost is BUCKET_COUNT * buffer_events * sizeof(MappingEvent).
//!
//! write() is thread-safe. flush(), written() and the destructor must not run concurrently with write().
class BucketWriter
{
  public:

	static constexpr std::size_t DEFAULT_BUFFER_EVENTS { 5461 };

	explicit BucketWriter( std::filesystem::path dir, std::size_t buffer_events = DEFAULT_BUFFER_EVENTS );

	BucketWriter( const BucketWriter& ) = delete;
	BucketWriter& operator=( const BucketWriter& ) = delete;
	BucketWriter( BucketWriter&& ) = delete;
	BucketWriter& operator=( BucketWriter&& ) = delete;

	//! Flushes every buffer. Errors are logged, not thrown, since a destructor must not throw.
	~BucketWriter();

	void write( const MappingEvent& event );

	//! Writes out every buffer; call before reading any bucket back.
	void flush();

	//! Total events accepted, buffered or not.
	std::uint64_t written() const;

  private:

	struct FileCloser
	{
		void operator()( std::FILE* file ) const noexcept;
	};

	using FilePtr = std::unique_ptr< std::FILE, FileCloser >;

	//! \pre The caller holds m_mutexes[ bucket ].
	void flushBucket( std::size_t bucket );

	std::filesystem::path m_dir;
	std::size_t m_buffer_events;

	//! One per bucket, guarding that bucket's file, buffer and counter.
	mutable std::vector< std::mutex > m_mutexes;

	std::vector< FilePtr > m_files {};
	std::vector< std::vector< MappingEvent > > m_buffers {};
	std::vector< std::uint64_t > m_counts {};
};

} // namespace idhan::hydrus::ptr
