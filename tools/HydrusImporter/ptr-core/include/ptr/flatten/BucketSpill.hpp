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

std::filesystem::path bucketPath( const std::filesystem::path& dir, std::size_t bucket );

std::vector< MappingEvent > readBucket( const std::filesystem::path& path );

class BucketWriter
{
  public:

	static constexpr std::size_t DEFAULT_BUFFER_EVENTS { 5461 };

	explicit BucketWriter( std::filesystem::path dir, std::size_t buffer_events = DEFAULT_BUFFER_EVENTS );

	BucketWriter( const BucketWriter& ) = delete;
	BucketWriter& operator=( const BucketWriter& ) = delete;
	BucketWriter( BucketWriter&& ) = delete;
	BucketWriter& operator=( BucketWriter&& ) = delete;

	~BucketWriter();

	void write( const MappingEvent& event );

	void flush();

	std::uint64_t written() const;

  private:

	struct FileCloser
	{
		void operator()( std::FILE* file ) const noexcept;
	};

	using FilePtr = std::unique_ptr< std::FILE, FileCloser >;

	void flushBucket( std::size_t bucket );

	std::filesystem::path m_dir;
	std::size_t m_buffer_events;

	mutable std::vector< std::mutex > m_mutexes;

	std::vector< FilePtr > m_files {};
	std::vector< std::vector< MappingEvent > > m_buffers {};
	std::vector< std::uint64_t > m_counts {};
};

} // namespace idhan::hydrus::ptr
