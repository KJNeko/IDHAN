#pragma once

#include <folly/File.h>

#include <cstddef>
#include <filesystem>
#include <memory>

#include "idhan/runtime/Task.hpp"

namespace idhan::server
{
class TemporaryStorage
{
	struct Impl;
	std::unique_ptr< Impl > m_impl;

  public:

	TemporaryStorage( std::filesystem::path parent );
	~TemporaryStorage();
	TemporaryStorage( const TemporaryStorage& ) = delete;
	TemporaryStorage& operator=( const TemporaryStorage& ) = delete;

	[[nodiscard]] runtime::Task<> start();
	[[nodiscard]] runtime::Task< folly::File > createUploadFile();
	[[nodiscard]] std::filesystem::path path() const;
	[[nodiscard]] runtime::Task<> drain();
};
} // namespace idhan::server
