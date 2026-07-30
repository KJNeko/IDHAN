//
// Created by kj16609 on 7/28/26.
//
#pragma once

#include <array>
#include <cstddef>
#include <expected>
#include <memory>
#include <string>
#include <vector>

#include "MetadataInfo.hpp"
#include "ThumbnailInfo.hpp"
#include "WorkerPool.hpp"
#include "ipc/Blob.hpp"

namespace idhan::modules
{

//! The input to a remote module call. The out-of-process analogue of ModuleCallData.
/** Carries the blob rather than a data_view: the bytes have to reach another process, and the only
 *  thing that can cross is the descriptor. */
struct RemoteCallData
{
	const ipc::Blob* blob { nullptr };
	std::string mime_name {};
	Json::Value extra {};

	//! How many module-initiated hops led here. Zero for a call the server originated.
	std::uint32_t depth { 0 };
};

//! One module, addressed across a process boundary.
/** Deliberately not derived from MetadataModuleI/ThumbnailerModuleI/GeneratorModuleI. Those are the
 *  interfaces a module *implements*, and they now only exist inside a worker; this is the handle the
 *  server calls through. It is addressed by (pool, module index) -- never by name, which is neither
 *  unique nor stable across builds. */
class RemoteModule
{
	std::shared_ptr< WorkerPool > m_pool;
	std::size_t m_module_index { 0 };
	std::string m_name {};
	ModuleType m_type { 0 };
	ModuleVersion m_version {};
	std::vector< std::string > m_mimes {};

	//! Builds the common part of a CALL body.
	[[nodiscard]] Json::Value baseBody( ipc::CallOp op, const RemoteCallData& data ) const;

  public:

	RemoteModule(
		std::shared_ptr< WorkerPool > pool,
		std::size_t module_index,
		std::string name,
		ModuleType type,
		ModuleVersion version,
		std::vector< std::string > mimes );

	[[nodiscard]] std::string_view name() const { return m_name; }

	[[nodiscard]] ModuleType type() const { return m_type; }

	[[nodiscard]] ModuleVersion version() const { return m_version; }

	[[nodiscard]] const std::vector< std::string >& handleableMimes() const { return m_mimes; }

	[[nodiscard]] bool canHandle( std::string_view mime ) const;

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

	[[nodiscard]] IDHANTask< std::expected< std::vector< std::byte >, ModuleError > > generate(
		RemoteCallData data,
		std::array< std::byte, 256 / 8 > desired_hash ) const;
};

} // namespace idhan::modules
