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
class OnnxEmbedder final : public idhan::EmbeddingModuleI
{
	ModelConfig m_config;

	//! Built in startup(), not in the constructor: --describe enumerates modules and must not pay
	//! session creation, which for a 400M-parameter model is seconds and gigabytes.
	std::unique_ptr< Ort::Env > m_env {};
	std::unique_ptr< Ort::Session > m_session {};
	std::unique_ptr< Ort::MemoryInfo > m_memory_info {};

	//! Built in startup() when the model ships a text tower. Null otherwise, and null when building
	//! it failed, in which case m_text_failure says why.
	std::unique_ptr< Ort::Session > m_text_session {};
	BpeTokenizer m_tokenizer {};

	//! Why text queries are being refused, when the model declares a text tower but cannot serve one.
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

	[[nodiscard]] bool threadSafe() override { return true; }

	[[nodiscard]] idhan::ModuleVersion version() override { return idhan::ModuleVersion { 1, 0, 0 }; }

	//! Loading a multi-gigabyte session per call is impractical, so the worker stays resident.
	[[nodiscard]] idhan::ModuleResidency residency() override { return idhan::ModuleResidency::PERSISTENT; }

	//! Derived from the size of the graphs on disk, since that is what the sessions hold resident.
	[[nodiscard]] std::size_t rssCeilingMb() override;

	void startup() override;

	void shutdown() override;

	[[nodiscard]] std::expected< idhan::EmbeddingInfo, idhan::ModuleError > embed( idhan::ModuleCallData& data )
		override;

	//! Whether this model ships a text tower.
	[[nodiscard]] bool supportsText() override
	{
		return !m_config.m_text_onnx_path.empty() && !m_config.m_tokenizer_path.empty();
	}

	[[nodiscard]] std::expected< idhan::EmbeddingInfo, idhan::ModuleError > embedText( std::string_view phrase )
		override;
};

} // namespace premade
