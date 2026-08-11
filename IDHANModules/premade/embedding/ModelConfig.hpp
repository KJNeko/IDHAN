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

//! One model as it sits on disk: the graph, and how its input has to be prepared.
/** Read from a sidecar json next to the .onnx rather than from the graph itself, so --describe can
 *  enumerate models without paying ONNX session creation for each one. The session is built later,
 *  in startup(), and the declared dimensionality is checked against the real output shape there --
 *  this file is a claim, not the truth. */
struct ModelConfig
{
	std::string m_model_name {};
	std::filesystem::path m_onnx_path {};
	std::size_t m_dimensions { 0 };
	idhan::PreprocessSpec m_preprocess {};
	std::string m_input_name { "pixel_values" };
	std::string m_output_name { "image_features" };
	//! Whether the exported graph already L2-normalises. When false the module normalises itself.
	bool m_normalized_output { true };
};

//! Reads every model directory under \p models_root.
/** A directory qualifies when it holds both model.onnx and model.json. Anything malformed is skipped
 *  with a log line rather than aborting the scan: one bad model directory must not cost the host
 *  every other model in the same library. */
[[nodiscard]] std::vector< ModelConfig > scanModels( const std::filesystem::path& models_root );

//! Parses one model.json.
[[nodiscard]] std::expected< ModelConfig, std::string > readModelConfig(
	const std::filesystem::path& config_path,
	const std::filesystem::path& onnx_path );

} // namespace premade
