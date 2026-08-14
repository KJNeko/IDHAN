#pragma once
#include <array>
#include <cstddef>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include "ModuleBase.hpp"

namespace idhan
{

//! The pixels a model wants, in the terms the host's thumbnail callback understands.
struct PreprocessSpec
{
	std::size_t m_width { 224 };
	std::size_t m_height { 224 };
	//! True for SigLIP2's "squash": resize to exactly width x height with aspect ratio discarded.
	bool m_force_exact { true };
	std::array< float, 3 > m_mean { { 0.5f, 0.5f, 0.5f } };
	std::array< float, 3 > m_std { { 0.5f, 0.5f, 0.5f } };
};

//! One embedding vector.
struct EmbeddingInfo
{
	//! L2-normalised by the module; length is always EmbeddingModuleI::dimensions().
	std::vector< float > m_vector {};
};

//! A module that turns a file into a vector.
class FGL_EXPORT EmbeddingModuleI : public ModuleBase
{
  public:

	EmbeddingModuleI() = delete;

	EmbeddingModuleI( ModuleCallbacks callbacks ) : ModuleBase( callbacks ) {}

	~EmbeddingModuleI() override;

	//! Stable identity of the model this instance wraps.
	[[nodiscard]] virtual std::string_view modelName() = 0;

	//! Output dimensionality. Fixed for the module's lifetime; the host builds halfvec(N) from it.
	[[nodiscard]] virtual std::size_t dimensions() = 0;

	//! The pixel size and normalisation this model requires.
	[[nodiscard]] virtual PreprocessSpec preprocess() = 0;

	//! Embeds one file.
	//! \param data The source file and its MIME (see ModuleCallData).
	//! \return An L2-normalised vector of dimensions() floats, or a ModuleError.
	[[nodiscard]] virtual std::expected< EmbeddingInfo, ModuleError > embed( ModuleCallData& data ) = 0;

	//! Whether this model has a text tower.
	[[nodiscard]] virtual bool supportsText() { return false; }

	//! Embeds a phrase into the same space as embed(). Must not be called unless supportsText().
	//! \return An L2-normalised vector of dimensions() floats, or a ModuleError.
	[[nodiscard]] virtual std::expected< EmbeddingInfo, ModuleError > embedText(
		[[maybe_unused]] std::string_view phrase )
	{
		return std::unexpected( ModuleError { "this model has no text encoder" } );
	}

	//! \return ModuleTypeFlags::EMBEDDING.
	ModuleType type() override;
};

} // namespace idhan
