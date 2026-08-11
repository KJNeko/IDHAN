//
// Created by kj16609 on 8/10/26.
//
#pragma once

#include <expected>
#include <filesystem>
#include <string>
#include <vector>

#include "EmbeddingModule.hpp"

namespace premade
{

//! One model as it sits on disk: its graphs, and how their input has to be prepared.
/** Discovered from a HuggingFace ONNX repository clone, because that is the whole of setup -- one
 *  `git clone` into the models directory, with nothing installed, converted or hand-written.
 *
 *  Nothing here is read from a file IDHAN authored. The dimensionality and tensor names come from the
 *  graphs themselves, the preprocessing from the repository's preprocessor_config.json. An optional
 *  model.json is still honoured for hand-prepared directories, but it cannot be required: upstream
 *  does not ship one, and a mandatory file the clone lacks would break the setup story. */
struct ModelConfig
{
	//! The directory name. The directory is the model -- this is a database key and a routing key, so
	//! it must be stable, and a name derived from a file's contents would change if that file did.
	std::string m_model_name {};

	std::filesystem::path m_onnx_path {}; //!< The image tower.
	std::size_t m_dimensions { 0 };
	idhan::PreprocessSpec m_preprocess {};
	std::string m_input_name { "pixel_values" };
	std::string m_output_name { "image_features" };
	//! Whether the exported graph already L2-normalises. Upstream ONNX exports generally do not --
	//! transformers returns raw pooled features -- so this defaults false and the module normalises.
	bool m_normalized_output { false };

	//! The text tower. Empty when the clone has none, which is a normal configuration.
	std::filesystem::path m_text_onnx_path {};
	std::string m_text_input_name { "input_ids" };
	std::string m_text_output_name { "text_features" };
	//! Zero means the graph left its sequence axis dynamic, as upstream's text tower does. The
	//! tokenizer then takes the length from tokenizer.json's own padding strategy, which is the
	//! model's actual statement of what it was trained with.
	std::size_t m_context_length { 0 };
	std::filesystem::path m_tokenizer_path {};
};

//! Reads every model directory under \p models_root.
/** A directory qualifies when a usable image tower can be found in it. Anything malformed is skipped
 *  with a log line rather than aborting the scan: one bad model directory must not cost the host
 *  every other model in the same library. */
[[nodiscard]] std::vector< ModelConfig > scanModels( const std::filesystem::path& models_root );

//! Discovers one model directory.
/** Accepts the upstream ONNX repository layout (`onnx/vision_model.onnx`, `onnx/text_model.onnx`,
 *  `preprocessor_config.json`, `tokenizer.json`) and the older flat layout (`model.onnx` beside a
 *  `model.json`). Where both a discovered value and a model.json entry exist, model.json wins -- it
 *  is only ever present because someone wrote it deliberately. */
[[nodiscard]] std::expected< ModelConfig, std::string > readModelDirectory( const std::filesystem::path& directory );

} // namespace premade
