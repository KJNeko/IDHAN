#pragma once

#include <cstddef>
#include <expected>
#include <filesystem>
#include <limits>
#include <span>
#include <string>

#include "ModuleBase.hpp"
#include "UniqueFd.hpp"

namespace idhan::ipc
{

inline constexpr std::size_t MAX_IPC_BLOB_BYTES { 512u * 1024u * 1024u };

class Blob
{
	UniqueFd m_fd {};
	void* m_mapping { nullptr };
	std::size_t m_size { 0 };

	Blob( UniqueFd fd, void* mapping, std::size_t size );

  public:

	Blob() = default;
	~Blob();

	Blob( const Blob& ) = delete;
	Blob& operator=( const Blob& ) = delete;

	Blob( Blob&& other ) noexcept;
	Blob& operator=( Blob&& other ) noexcept;

	[[nodiscard]] static std::expected< Blob, std::string > fromFile( const std::filesystem::path& path );

	[[nodiscard]] static std::expected< Blob, std::string > fromFd( int source, std::size_t size );

	[[nodiscard]] static std::expected< Blob, std::string > fromBytes( std::span< const std::byte > bytes );

	[[nodiscard]] static std::expected< Blob, std::string > adopt( UniqueFd fd );

	//! Adopts a worker-produced immutable memfd, rejecting mutable or oversized descriptors.
	[[nodiscard]] static std::expected< Blob, std::string > adoptSealed(
		UniqueFd fd,
		std::size_t maximum_size = MAX_IPC_BLOB_BYTES );

	[[nodiscard]] data_view view() const
	{
		return data_view { static_cast< const std::uint8_t* >( m_mapping ), m_size };
	}

	[[nodiscard]] std::span< const std::byte > bytes() const
	{
		return std::span< const std::byte > { static_cast< const std::byte* >( m_mapping ), m_size };
	}

	[[nodiscard]] int fd() const { return m_fd.get(); }

	void closeDescriptor() { m_fd.reset(); }

	[[nodiscard]] std::size_t size() const { return m_size; }

	[[nodiscard]] bool valid() const { return static_cast< bool >( m_fd ) || m_mapping != nullptr; }
};

} // namespace idhan::ipc
