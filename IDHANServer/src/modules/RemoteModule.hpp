#pragma once

#include <array>
#include <cstddef>
#include <expected>
#include <memory>
#include <string>
#include <vector>

#include "CallInput.hpp"
#include "MimeIDs.hpp"
#include "EmbeddingModule.hpp"
#include "MetadataInfo.hpp"
#include "ThumbnailInfo.hpp"
#include "WorkerPool.hpp"
#include "ipc/Blob.hpp"

namespace idhan::modules
{

inline constexpr static std::uint32_t SERVER_ORIGINATED { 0 };

struct RemoteCallData
{
	std::shared_ptr< const CallInput > input {};
	MimeID mime_id { mime_ids::INVALID };
	Json::Value extra {};

	//! How many module-initiated hops led here. Zero for a call the server originated.
	std::uint32_t depth { SERVER_ORIGINATED };
};

class RemoteModule
{
	std::shared_ptr< WorkerPool > m_pool;
	std::size_t m_module_index { 0 };
	std::string m_name {};
	ModuleType m_type { 0 };
	ModuleVersion m_version {};
	std::vector< MimeID > m_mimes {};
	//! EMBEDDING modules only. Empty/zero for every other kind.
	std::string m_model_name {};
	std::uint32_t m_dimensions { 0 };
	bool m_supports_text { false };

	[[nodiscard]] Json::Value baseBody( ipc::CallOp op, const RemoteCallData& data ) const;

  public:

	RemoteModule(
		std::shared_ptr< WorkerPool > pool,
		std::size_t module_index,
		std::string name,
		ModuleType type,
		ModuleVersion version,
		std::vector< MimeID > mimes,
		std::string model_name = {},
		std::uint32_t dimensions = 0,
		bool supports_text = false );

	[[nodiscard]] std::string_view name() const { return m_name; }

	[[nodiscard]] ModuleType type() const { return m_type; }

	[[nodiscard]] ModuleVersion version() const { return m_version; }

	[[nodiscard]] const std::vector< MimeID >& handleableMimes() const { return m_mimes; }

	[[nodiscard]] bool canHandle( MimeID mime_id ) const;

	[[nodiscard]] IDHANTask< std::expected< MetadataInfo, ModuleError > > parseFile( RemoteCallData data ) const;

	//! Raw interleaved RGB pixels.
	[[nodiscard]] IDHANTask< std::expected< ThumbnailInfo, ModuleError > > createThumbnailRaw(
		RemoteCallData data,
		std::size_t width,
		std::size_t height ) const;

	//! An encoded image (PNG, despite the historical WEBP naming).
	[[nodiscard]] IDHANTask< std::expected< ThumbnailInfo, ModuleError > > createThumbnailFile(
		RemoteCallData data,
		std::size_t width,
		std::size_t height ) const;

	//! Narrows an already resolved base mime to a refined mime id (see MimeIDs.hpp).
	[[nodiscard]] IDHANTask< std::expected< MimeID, ModuleError > > parseMime( RemoteCallData data ) const;

	//! Generates a derived file, returned as the memfd the worker wrote it into.
	[[nodiscard]] IDHANTask< std::expected< ipc::Blob, ModuleError > > generate(
		RemoteCallData data,
		std::array< std::byte, 256 / 8 > desired_hash ) const;

	[[nodiscard]] std::string_view modelName() const { return m_model_name; }

	[[nodiscard]] std::uint32_t dimensions() const { return m_dimensions; }

	[[nodiscard]] IDHANTask< std::expected< EmbeddingInfo, ModuleError > > embed( RemoteCallData data ) const;

	[[nodiscard]] bool supportsText() const { return m_supports_text; }

	[[nodiscard]] IDHANTask< std::expected< EmbeddingInfo, ModuleError > > embedText( std::string phrase ) const;
};

} // namespace idhan::modules
