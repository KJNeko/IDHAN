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

struct PreprocessSpec
{
	std::size_t m_width { 224 };
	std::size_t m_height { 224 };
	//! True for SigLIP2's "squash": resize to exactly width x height with aspect ratio discarded.
	bool m_force_exact { true };
	std::array< float, 3 > m_mean { { 0.5f, 0.5f, 0.5f } };
	std::array< float, 3 > m_std { { 0.5f, 0.5f, 0.5f } };
};

struct EmbeddingInfo
{
	//! L2-normalised by the module; length is always EmbeddingModuleI::dimensions().
	std::vector< float > m_vector {};
};

class FGL_EXPORT EmbeddingModuleI : public ModuleBase
{
  public:

	EmbeddingModuleI() = delete;

	EmbeddingModuleI( ModuleCallbacks callbacks ) : ModuleBase( callbacks ) {}

	~EmbeddingModuleI() override;

	[[nodiscard]] virtual std::string_view modelName() = 0;

	//! Output dimensionality. Fixed for the module's lifetime; the host builds halfvec(N) from it.
	[[nodiscard]] virtual std::size_t dimensions() = 0;

	[[nodiscard]] virtual PreprocessSpec preprocess() = 0;

	//! Embeds one file.
	//! \return An L2-normalised vector of dimensions() floats, or a ModuleError.
	[[nodiscard]] virtual std::expected< EmbeddingInfo, ModuleError > embed( ModuleCallData& data ) = 0;

	[[nodiscard]] virtual bool supportsText() { return false; }

	//! Embeds a phrase into the same space as embed(). Must not be called unless supportsText().
	//! \return An L2-normalised vector of dimensions() floats, or a ModuleError.
	[[nodiscard]] virtual std::expected< EmbeddingInfo, ModuleError > embedText(
		[[maybe_unused]] std::string_view phrase )
	{
		return std::unexpected( ModuleError { "this model has no text encoder" } );
	}

	ModuleType type() override;
};

} // namespace idhan
