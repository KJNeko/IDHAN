#pragma once

#include <folly/File.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <vector>

#include "Messages.hpp"
#include "idhan/runtime/Task.hpp"

namespace idhan::ipc::binary
{

// A result keeps descriptor ownership adjacent to the decoded wire result.
// Input::fd_index refers to the corresponding descriptor in this vector.
struct OwnedResult
{
	Result result;
	std::vector< folly::File > descriptors;
};

struct OwnedCallbackResult
{
	CallbackResult result;
	std::vector< folly::File > descriptors;
};

struct RunnerConfig
{
	std::filesystem::path library_path;
	std::uint32_t max_payload { 64U * 1024U * 1024U };
};

using RunnerCallback = std::function< runtime::Task< OwnedCallbackResult >( Callback, std::vector< folly::File > ) >;

class ModuleRunner
{
	struct Impl;
	std::unique_ptr< Impl > m_impl;

  public:

	explicit ModuleRunner( RunnerConfig );
	~ModuleRunner();
	ModuleRunner( ModuleRunner&& ) noexcept;
	ModuleRunner& operator=( ModuleRunner&& ) noexcept;
	ModuleRunner( const ModuleRunner& ) = delete;
	ModuleRunner& operator=( const ModuleRunner& ) = delete;

	[[nodiscard]] runtime::Task<> negotiate( int socket );
	[[nodiscard]] runtime::Task< OwnedResult > dispatch( Call, std::vector< folly::File > );
	void setCallbackHandler( RunnerCallback );
	[[nodiscard]] runtime::Task<> serve( int socket );
	[[nodiscard]] runtime::Task<> shutdown();
	[[nodiscard]] const Manifest& manifest() const;
};

} // namespace idhan::ipc::binary
