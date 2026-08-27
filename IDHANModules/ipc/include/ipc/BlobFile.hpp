#pragma once

#include <cstddef>
#include <expected>
#include <span>
#include <utility>

#include "Blob.hpp"
#include "ModuleFile.hpp"

namespace idhan::ipc
{

class BlobFile final : public ModuleFile
{
	Blob m_blob;

  public:

	explicit BlobFile( Blob blob ) : m_blob( std::move( blob ) ) {}

	[[nodiscard]] std::size_t size() const override { return m_blob.size(); }

	[[nodiscard]] std::span< const std::byte > mapped() const override { return m_blob.bytes(); }

	[[nodiscard]] int fd() const { return m_blob.fd(); }

	[[nodiscard]] std::expected< std::size_t, ModuleError > read( std::span< std::byte > out, std::size_t offset )
		const override;
};

} // namespace idhan::ipc
