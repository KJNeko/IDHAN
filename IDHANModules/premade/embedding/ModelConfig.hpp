#pragma once

#include <expected>
#include <filesystem>
#include <string>
#include <vector>

#include "EmbeddingModule.hpp"

namespace premade
{

//! One model as it sits on disk: its graphs, and how their input has to be prepared.
struct ModelConfig
{
	//! The directory name.
	std::string m_model_name {};

	std::filesystem::path m_onnx_path {}; //!< The image tower.
	std::size_t m_dimensions { 0 };
	idhan::PreprocessSpec m_preprocess {};
	std::string m_input_name { "pixel_values" };
	std::string m_output_name { "image_features" };
	//! Whether the exported graph already L2-normalises.
	bool m_normalized_output { false };

	//! The text tower. Empty when the clone has none, which is a normal configuration.
	std::filesystem::path m_text_onnx_path {};
	std::string m_text_input_name { "input_ids" };
	std::string m_text_output_name { "text_features" };
	//! Zero means the graph left its sequence axis dynamic, as upstream's text tower does.
	std::size_t m_context_length { 0 };
	std::filesystem::path m_tokenizer_path {};
};

//! Reads every model directory under \p models_root.
[[nodiscard]] std::vector< ModelConfig > scanModels( const std::filesystem::path& models_root );

//! Discovers one model directory.
[[nodiscard]] std::expected< ModelConfig, std::string > readModelDirectory( const std::filesystem::path& directory );

} // namespace premade
