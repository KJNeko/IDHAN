//
// Created by kj16609 on 8/10/26.
//
#pragma once

#include <cstddef>
#include <expected>
#include <memory>
#include <onnxruntime_cxx_api.h>
#include <vector>

#include "EmbeddingModule.hpp"
#include "ModelConfig.hpp"
#include "Tokenizer.hpp"

namespace premade
{

//! One SigLIP-family image encoder, hosted by ONNX Runtime on the CPU.
/** Does not decode images. It asks the host to do that through ModuleCallbacks::thumbnail, which
 *  reaches the vips thumbnailer -- so this module handles every image format vips does without
 *  linking a codec, and only has to do arithmetic on the pixels that come back.
 *
 *  One call, one forward pass. Batching exists to stop a GPU idling between kernel launches; on a
 *  CPU there is no launch latency to hide and a batch of N costs what N passes cost, so coalescing
 *  calls buys nothing and costs a queue, a gather window that delays every lone call, and a shutdown
 *  handshake to unpark whoever is waiting in it. Concurrency comes from the worker's pool instead:
 *  each pool thread runs its own single-image pass on its own core. */
class OnnxEmbedder final : public idhan::EmbeddingModuleI
{
	ModelConfig m_config;

	//! Built in startup(), not in the constructor: --describe enumerates modules and must not pay
	//! session creation, which for a 400M-parameter model is seconds and gigabytes.
	std::unique_ptr< Ort::Env > m_env {};
	std::unique_ptr< Ort::Session > m_session {};
	std::unique_ptr< Ort::MemoryInfo > m_memory_info {};

	//! Built in startup() when the model ships a text tower. Null otherwise, and null when building
	//! it failed -- in which case m_text_failure says why.
	std::unique_ptr< Ort::Session > m_text_session {};
	BpeTokenizer m_tokenizer {};

	//! Why text queries are being refused, when the model declares a text tower but cannot serve one.
	/** Kept as a string rather than a bool because the caller sees this: "no text encoder" is useless
	 *  next to naming the file that would not load. */
	std::string m_text_failure {};

	//! Converts packed 8-bit RGB into the normalised CHW tensor the graph expects.
	[[nodiscard]] std::vector< float > toTensor( const std::vector< std::byte >& pixels ) const;

	//! Runs one forward pass over \p tensor and returns the L2-normalised output vector.
	[[nodiscard]] std::expected< std::vector< float >, idhan::ModuleError > runOne( std::vector< float >& tensor );

	//! Runs one text forward pass over \p ids and returns the L2-normalised output vector.
	[[nodiscard]] std::expected< std::vector< float >, idhan::ModuleError > runText( std::vector< std::int64_t >& ids );

  public:

	OnnxEmbedder() = delete;

	OnnxEmbedder( idhan::ModuleCallbacks callbacks, ModelConfig config );

	~OnnxEmbedder() override;

	[[nodiscard]] std::string_view name() override { return m_config.m_model_name; }

	[[nodiscard]] std::string_view modelName() override { return m_config.m_model_name; }

	[[nodiscard]] std::size_t dimensions() override { return m_config.m_dimensions; }

	[[nodiscard]] idhan::PreprocessSpec preprocess() override { return m_config.m_preprocess; }

	//! Ort::Session::Run is safe to call concurrently, and this is where all the throughput comes from
	//! now that there is no batcher: the session is built with one intra-op thread, so N pool threads
	//! each running a pass is what puts N cores to work. Returning false would serialise the module
	//! behind the worker's per-module lock and leave the machine embedding on a single core.
	[[nodiscard]] bool threadSafe() override { return true; }

	[[nodiscard]] idhan::ModuleVersion version() override { return idhan::ModuleVersion { 1, 0, 0 }; }

	//! Loading a multi-gigabyte session per call would be absurd, so the worker stays resident.
	[[nodiscard]] idhan::ModuleResidency residency() override { return idhan::ModuleResidency::PERSISTENT; }

	void startup() override;

	void shutdown() override;

	[[nodiscard]] std::expected< idhan::EmbeddingInfo, idhan::ModuleError > embed( idhan::ModuleCallData& data )
		override;

	//! Whether this model ships a text tower.
	/** Answered from what is on disk, deliberately, because the worker announces its manifest before
	 *  it calls startup() -- so no session exists yet and none can be created here without paying the
	 *  very cost --describe is cheap in order to avoid.
	 *
	 *  The consequence is that a text tower which then fails to load is still advertised, and
	 *  embedText() refuses with the reason instead. Advertising nothing until startup() had run would
	 *  mean the host could never register a model's text support at all. */
	[[nodiscard]] bool supportsText() override
	{
		return !m_config.m_text_onnx_path.empty() && !m_config.m_tokenizer_path.empty();
	}

	[[nodiscard]] std::expected< idhan::EmbeddingInfo, idhan::ModuleError > embedText( std::string_view phrase )
		override;
};

} // namespace premade
