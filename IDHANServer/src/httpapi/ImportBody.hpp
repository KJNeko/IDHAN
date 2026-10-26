#pragma once

#include <expected>
#include <memory>
#include <span>
#include <string>

#include "idhan/http/RequestContext.hpp"

namespace idhan::httpapi
{

// Owns the byte view used across MIME detection and storage coroutine suspensions.
// Spilled uploads are mapped read-only instead of copied back into heap memory.
class ImportBody
{
	struct Unmap
	{
		std::size_t size {};
		void operator()( const std::byte* bytes ) const noexcept;
	};

	std::unique_ptr< folly::IOBuf > m_buffer {};
	std::unique_ptr< const std::byte, Unmap > m_mapping { nullptr, Unmap {} };
	SHA256 m_hash {};

	ImportBody() = default;

  public:

	ImportBody( ImportBody&& ) noexcept = default;
	ImportBody& operator=( ImportBody&& ) noexcept = default;

	[[nodiscard]] static std::expected< ImportBody, std::string > create( http::RequestBody body );
	// Borrowed until this ImportBody is destroyed or assigned to.
	[[nodiscard]] std::span< const std::byte > bytes() const noexcept;
	[[nodiscard]] SHA256 hash() const;
};

} // namespace idhan::httpapi
