#pragma once

#include <json/value.h>

#include <cstddef>
#include <expected>
#include <filesystem>
#include <memory>
#include <span>
#include <string>

#include "ipc/Blob.hpp"
#include "ipc/UniqueFd.hpp"

namespace idhan::modules
{

class CallInput
{
	//! The descriptor the worker will map. A record's O_RDONLY file, or the memfd below.
	ipc::UniqueFd m_file {};

	//! Held only when the bytes had to be staged into anonymous memory. Owns the descriptor in that
	//! case, and gives the server a mapping to scan for a MIME when it has no other way to know one.
	ipc::Blob m_blob {};

	std::size_t m_size { 0 };

	CallInput() = default;

  public:

	~CallInput() = default;

	CallInput( const CallInput& ) = delete;
	CallInput& operator=( const CallInput& ) = delete;
	CallInput( CallInput&& ) noexcept = default;
	CallInput& operator=( CallInput&& ) noexcept = default;

	[[nodiscard]] static std::expected< CallInput, std::string > forPath( const std::filesystem::path& path );

	[[nodiscard]] static std::expected< CallInput, std::string > forBlob( ipc::Blob blob );

	//! Stages \p bytes into anonymous memory and wraps the result.
	[[nodiscard]] static std::expected< CallInput, std::string > forBytes( std::span< const std::byte > bytes );

	[[nodiscard]] static std::expected< std::shared_ptr< const CallInput >, std::string > sharedForBytes(
		std::span< const std::byte > bytes );

	//! The descriptor to attach to a CALL frame. Borrowed; ownership stays here.
	[[nodiscard]] int fd() const;

	[[nodiscard]] std::size_t size() const { return m_size; }

	//! The mapped bytes, when the input was staged from memory; empty when it is a file on disk.
	[[nodiscard]] const ipc::Blob& blob() const { return m_blob; }
};

} // namespace idhan::modules
