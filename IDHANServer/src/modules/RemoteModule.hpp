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

#include "CallInput.hpp"
#include "EmbeddingModule.hpp"
#include "MetadataInfo.hpp"
#include "ThumbnailInfo.hpp"
#include "WorkerPool.hpp"
#include "ipc/Blob.hpp"

namespace idhan::modules
{

//! The input to a remote module call. The out-of-process analogue of ModuleCallData.
/** Carries a CallInput rather than a data_view: the bytes have to reach another process, and the
 *  only thing that can cross is a descriptor.
 *
 *  Shared rather than borrowed because the input outlives this struct in one specific case that
 *  matters. A module can hand its own input back through a callback, and the answer to that is to
 *  reuse the descriptor the server already holds instead of shipping the file again -- which means
 *  the worker registry has to keep the input alive for as long as the call is in flight, alongside
 *  whatever coroutine frame originally created it. */
struct RemoteCallData
{
	std::shared_ptr< const CallInput > input {};
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
	//! EMBEDDING modules only. Empty/zero for every other kind.
	std::string m_model_name {};
	std::uint32_t m_dimensions { 0 };

	//! Builds the common part of a CALL body.
	[[nodiscard]] Json::Value baseBody( ipc::CallOp op, const RemoteCallData& data ) const;

  public:

	RemoteModule(
		std::shared_ptr< WorkerPool > pool,
		std::size_t module_index,
		std::string name,
		ModuleType type,
		ModuleVersion version,
		std::vector< std::string > mimes,
		std::string model_name = {},
		std::uint32_t dimensions = 0 );

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

	//! Generates a derived file, returned as the memfd the worker wrote it into.
	/** A blob rather than a vector because generator output is large by nature -- an extracted
	 *  archive member, a decoded page -- and the worker already wrote it exactly once, into shared
	 *  memory. Copying it into a vector here would put a second copy in the server's heap on the way
	 *  to handing the descriptor straight back out again. */
	[[nodiscard]] IDHANTask< std::expected< ipc::Blob, ModuleError > > generate(
		RemoteCallData data,
		std::array< std::byte, 256 / 8 > desired_hash ) const;

	//! The model this module wraps. Empty unless type() includes EMBEDDING.
	[[nodiscard]] std::string_view modelName() const { return m_model_name; }

	//! The width of the vectors this module produces. Zero unless type() includes EMBEDDING.
	[[nodiscard]] std::uint32_t dimensions() const { return m_dimensions; }

	//! Embeds one file into an L2-normalised vector of dimensions() floats.
	[[nodiscard]] IDHANTask< std::expected< EmbeddingInfo, ModuleError > > embed( RemoteCallData data ) const;
};

} // namespace idhan::modules
