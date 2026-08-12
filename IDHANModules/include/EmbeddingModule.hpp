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
/** Populated from the model's sidecar json, which the export script writes from open_clip's own
 *  preprocess_cfg rather than by hand -- the preprocessing has to match how the model was trained,
 *  and a value copied by a human is a value that can drift. */
struct PreprocessSpec
{
	std::size_t m_width { 224 };
	std::size_t m_height { 224 };
	//! True for SigLIP2's "squash": resize to exactly width x height with aspect ratio discarded.
	//! Requests resize_mode "force" from the thumbnailer. A model trained on squashed input that is
	//! handed aspect-preserved input produces wrong vectors and no error anywhere, so this is not a
	//! cosmetic setting.
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
/** Deliberately has no handleableMimes(). An embedding module does not decode images -- it asks the
 *  host to do that through ModuleCallbacks::thumbnail -- so what it can handle is "whatever some
 *  thumbnailer handles", which only the server knows. Routing is therefore by model name, and the
 *  caller filters candidate records by asking whether any thumbnailer covers their MIME.
 *
 *  A pleasant side effect: because this interface does not declare handleableMimes(), the EMBEDDING
 *  flag can be combined with another interface flag on one module without the ambiguity that keeps
 *  the other three to one flag each in practice. */
class FGL_EXPORT EmbeddingModuleI : public ModuleBase
{
  public:

	EmbeddingModuleI() = delete;

	EmbeddingModuleI( ModuleCallbacks callbacks ) : ModuleBase( callbacks ) {}

	~EmbeddingModuleI() override;

	//! Stable identity of the model this instance wraps.
	/** Unlike ModuleBase::name(), which is documented as logs-only and is neither unique nor stable
	 *  across builds, this is a database key (embedding_models.model_name) and the host's routing
	 *  key. It must be both unique and stable. */
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
	/** False is a normal configuration rather than an error: a model may ship image-only, its text
	 *  graph may be absent, or its tokenizer may have failed the parity check at startup. Callers
	 *  ask before sending text rather than discovering it from a failed call.
	 *
	 *  Defaulted, not pure, so an existing embedding module keeps compiling unchanged. */
	[[nodiscard]] virtual bool supportsText() { return false; }

	//! Embeds a phrase into the same space as embed().
	/** Takes no file, so none of the file plumbing applies -- this is the one call that operates on
	 *  nothing but a string. Must not be called unless supportsText().
	 *  \return An L2-normalised vector of dimensions() floats, or a ModuleError. */
	[[nodiscard]] virtual std::expected< EmbeddingInfo, ModuleError > embedText(
		[[maybe_unused]] std::string_view phrase )
	{
		return std::unexpected( ModuleError { "this model has no text encoder" } );
	}

	//! \return ModuleTypeFlags::EMBEDDING.
	ModuleType type() override;
};

} // namespace idhan
