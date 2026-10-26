#pragma once

#include <folly/File.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>

#include "idhan/runtime/Task.hpp"

namespace idhan::server
{
struct FileStat
{
	std::uint64_t size { 0 };
	std::filesystem::file_time_type modified;
	std::filesystem::file_time_type changed;
};

class AsyncFileService
{
	struct Impl;
	std::unique_ptr< Impl > m_impl;

  public:

	AsyncFileService();
	~AsyncFileService();
	AsyncFileService( const AsyncFileService& ) = delete;
	AsyncFileService& operator=( const AsyncFileService& ) = delete;

	[[nodiscard]] runtime::Task< folly::File > open( const std::filesystem::path& );
	[[nodiscard]] runtime::Task< FileStat > stat( const std::filesystem::path& );
	[[nodiscard]] runtime::Task< std::size_t > read( folly::File&, std::span< std::byte >, std::uint64_t offset );
	[[nodiscard]] runtime::Task< std::size_t > write(
		folly::File&,
		std::span< const std::byte >,
		std::uint64_t offset );
	[[nodiscard]] runtime::Task<> copy( folly::File&, folly::File&, std::uint64_t size );
	[[nodiscard]] runtime::Task<> drain();
};
} // namespace idhan::server
