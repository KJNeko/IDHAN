#include "ModelConfig.hpp"

#include <json/reader.h>
#include <json/value.h>
#include <spdlog/spdlog.h>

#include <array>
#include <format>
#include <fstream>
#include <sstream>

#include "OnnxShape.hpp"

namespace premade
{

//! Reads a 3-element float array (mean/std) out of \p json, leaving \p out alone when absent.
void readTriple( const Json::Value& json, std::array< float, 3 >& out )
{
	if ( !json.isArray() || json.size() != 3 ) return;

	for ( Json::ArrayIndex index = 0; index < 3; ++index )
	{
		if ( json[ index ].isNumeric() ) out[ index ] = static_cast< float >( json[ index ].asDouble() );
	}
}

//! Parses a json file, returning an empty value when it is absent or malformed.
/** Absence is not an error at any call site here -- every one of these files is optional, and a
 *  directory missing one falls back to what the graph declares. */
[[nodiscard]] Json::Value readJsonFile( const std::filesystem::path& path )
{
	std::ifstream file { path };
	if ( !file ) return {};

	std::stringstream buffer {};
	buffer << file.rdbuf();

	Json::Value json {};
	Json::CharReaderBuilder builder {};
	std::string errors {};
	std::istringstream input { buffer.str() };

	if ( !Json::parseFromStream( builder, input, &json, &errors ) )
	{
		spdlog::warn( "Ignoring {}: not valid json ({})", path.string(), errors );
		return {};
	}

	return json;
}

//! The precision variants upstream publishes, in the order they are preferred.
/** An operator who fetched only one variant gets that one without having to rename it. fp32 first
 *  because inference here is CPU-only: fp16 is a GPU memory optimisation the CPU provider has to
 *  undo, and the quantized forms trade accuracy for size. */
constexpr std::array< std::string_view, 7 > VARIANT_SUFFIXES {
	{ "", "_fp16", "_q4f16", "_q4", "_int8", "_uint8", "_quantized" }
};

//! Finds a tower's graph, honouring whichever precision variant is actually present.
[[nodiscard]] std::filesystem::path findTower( const std::filesystem::path& directory, const std::string_view stem )
{
	for ( const auto suffix : VARIANT_SUFFIXES )
	{
		// The upstream layout keeps graphs in onnx/; the flat form is what a hand-prepared directory
		// or an older IDHAN export looks like.
		const std::array< std::filesystem::path, 2 > candidates {
			{ directory / "onnx" / std::format( "{}{}.onnx", stem, suffix ),
			  directory / std::format( "{}{}.onnx", stem, suffix ) }
		};

		for ( const auto& candidate : candidates )
		{
			if ( std::filesystem::exists( candidate ) ) return candidate;
		}
	}

	return {};
}

//! Names an embedding output goes by, most specific first.
/** transformers exports declare `last_hidden_state` before `pooler_output`, and the first of those is
 *  a per-patch grid -- [batch, 196, 768] for a 224px ViT/16. Its trailing dimension is 768, the same
 *  as the real embedding's, so a loader that took output 0 and read `shape.back()` would find a
 *  plausible width and then embed patch grids. Selection is therefore by name, never by position. */
constexpr std::array< std::string_view, 4 > EMBEDDING_OUTPUT_NAMES {
	{ "pooler_output", "image_embeds", "text_embeds", "image_features" }
};

//! Picks the output that carries the embedding, and reports its width.
/** Falls back to structure when no name matches: an embedding is rank 2 ([batch, width]) with a fixed
 *  trailing axis, which the per-token and per-patch outputs are not. */
[[nodiscard]] const GraphTensorInfo* chooseEmbeddingOutput( const GraphInterface& interface )
{
	for ( const auto name : EMBEDDING_OUTPUT_NAMES )
	{
		if ( const auto* const found { interface.findOutput( name ) };
		     found != nullptr && found->m_shape.size() == 2 && found->m_shape.back() > 0 )
			return found;
	}

	for ( const auto& output : interface.m_outputs )
	{
		if ( output.m_shape.size() == 2 && output.m_shape.back() > 0 ) return &output;
	}

	return nullptr;
}

//! The width of whatever \p output declares, or zero when it is dynamic or absent.
[[nodiscard]] std::size_t declaredWidth( const GraphTensorInfo* const output )
{
	if ( output == nullptr || output->m_shape.empty() ) return 0;

	const auto back { output->m_shape.back() };
	return back > 0 ? static_cast< std::size_t >( back ) : 0;
}

std::expected< ModelConfig, std::string > readModelDirectory( const std::filesystem::path& directory )
{
	ModelConfig config {};

	// The directory is the model. Deriving the name from a file's contents would mean a model whose
	// database key changes when that file is edited.
	config.m_model_name = directory.filename().string();

	if ( config.m_model_name.empty() ) return std::unexpected( std::string { "model directory has no name" } );

	// vision_model is upstream's name; model is what IDHAN's own exporter wrote.
	config.m_onnx_path = findTower( directory, "vision_model" );
	if ( config.m_onnx_path.empty() ) config.m_onnx_path = findTower( directory, "model" );

	if ( config.m_onnx_path.empty() )
		return std::unexpected(
			std::string { "no image tower here (looked for onnx/vision_model.onnx and model.onnx)" } );

	// Read before anything can override it: this is the only source that cannot be stale, since it is
	// the graph that will actually run.
	const auto vision { readGraphInterface( config.m_onnx_path ) };
	if ( !vision ) return std::unexpected( vision.error() );

	config.m_input_name = vision->m_inputs.front().m_name;

	const auto* const vision_output { chooseEmbeddingOutput( *vision ) };

	// A graph with no rank-2 fixed-width output is one this module cannot use: without a width there
	// is no halfvec column to create, and without knowing which tensor is the embedding there is
	// nothing meaningful to store in it.
	if ( vision_output == nullptr )
		return std::unexpected( std::format( "{} declares no embedding output", config.m_onnx_path.string() ) );

	config.m_output_name = vision_output->m_name;
	config.m_dimensions = declaredWidth( vision_output );

	// Geometry deliberately does NOT come from the graph. Upstream exports declare pixel_values as
	// fully dynamic ([0,0,0,0]) so one graph serves every resolution, which means the only statement
	// of the size this model was trained at is preprocessor_config.json.
	if ( const auto preprocess { readJsonFile( directory / "preprocessor_config.json" ) }; preprocess.isObject() )
	{
		readTriple( preprocess[ "image_mean" ], config.m_preprocess.m_mean );
		readTriple( preprocess[ "image_std" ], config.m_preprocess.m_std );

		if ( const auto& size { preprocess[ "size" ] }; size.isObject() )
		{
			if ( size[ "height" ].isIntegral() )
				config.m_preprocess.m_height = static_cast< std::size_t >( size[ "height" ].asInt64() );
			if ( size[ "width" ].isIntegral() )
				config.m_preprocess.m_width = static_cast< std::size_t >( size[ "width" ].asInt64() );
		}
	}

	// SiglipImageProcessor resizes to exactly size.height x size.width with no aspect preservation,
	// which is the forced resize the module already asks the host for.
	config.m_preprocess.m_force_exact = true;

	config.m_text_onnx_path = findTower( directory, "text_model" );

	if ( !config.m_text_onnx_path.empty() )
	{
		const auto text { readGraphInterface( config.m_text_onnx_path ) };

		if ( !text )
		{
			// Degraded rather than fatal: an unreadable text tower costs text queries and nothing
			// else, and image embedding is the more important half.
			spdlog::warn(
				"Model '{}' has a text tower that could not be read ({}); text queries will be refused",
				config.m_model_name,
				text.error() );
			config.m_text_onnx_path.clear();
		}
		else if ( const auto text_width { declaredWidth( chooseEmbeddingOutput( *text ) ) };
		          text_width != 0 && text_width != config.m_dimensions )
		{
			// Both towers must land in the same space, or comparing a phrase to an image is
			// meaningless. Differing widths mean the two files do not belong together.
			spdlog::warn(
				"Model '{}' has a text tower of width {} but an image tower of width {}; refusing to use it",
				config.m_model_name,
				text_width,
				config.m_dimensions );
			config.m_text_onnx_path.clear();
		}
		else
		{
			config.m_text_input_name = text->m_inputs.front().m_name;

			const auto* const text_output { chooseEmbeddingOutput( *text ) };
			if ( text_output != nullptr ) config.m_text_output_name = text_output->m_name;

			// [batch, sequence]. A fixed sequence length is authoritative; upstream leaves it
			// dynamic, and zero tells the tokenizer to use tokenizer.json's padding strategy.
			if ( const auto& shape { text->m_inputs.front().m_shape }; shape.size() == 2 && shape[ 1 ] > 0 )
				config.m_context_length = static_cast< std::size_t >( shape[ 1 ] );

			if ( const auto tokenizer { directory / "tokenizer.json" }; std::filesystem::exists( tokenizer ) )
			{
				config.m_tokenizer_path = tokenizer;
			}
			else
			{
				spdlog::warn(
					"Model '{}' has a text tower but no tokenizer.json; text queries will be refused",
					config.m_model_name );
				config.m_text_onnx_path.clear();
			}
		}
	}

	// Last, so a deliberately written file outranks everything discovered. Only ever present because
	// someone put it there.
	if ( const auto overrides { readJsonFile( directory / "model.json" ) }; overrides.isObject() )
	{
		if ( overrides[ "model_name" ].isString() ) config.m_model_name = overrides[ "model_name" ].asString();

		if ( overrides[ "dimensions" ].isIntegral() && overrides[ "dimensions" ].asInt64() > 0 )
			config.m_dimensions = static_cast< std::size_t >( overrides[ "dimensions" ].asInt64() );

		if ( overrides[ "input_name" ].isString() ) config.m_input_name = overrides[ "input_name" ].asString();
		if ( overrides[ "output_name" ].isString() ) config.m_output_name = overrides[ "output_name" ].asString();

		if ( overrides[ "normalized_output" ].isBool() )
			config.m_normalized_output = overrides[ "normalized_output" ].asBool();

		readTriple( overrides[ "mean" ], config.m_preprocess.m_mean );
		readTriple( overrides[ "std" ], config.m_preprocess.m_std );

		if ( const auto& size { overrides[ "image_size" ] }; size.isArray() && size.size() == 2 )
		{
			config.m_preprocess.m_width = static_cast< std::size_t >( size[ 0 ].asInt64() );
			config.m_preprocess.m_height = static_cast< std::size_t >( size[ 1 ].asInt64() );
		}
	}

	for ( const float value : config.m_preprocess.m_std )
	{
		if ( value == 0.0f )
			return std::unexpected( std::string { "a zero in std would divide by zero during preprocessing" } );
	}

	return config;
}

std::vector< ModelConfig > scanModels( const std::filesystem::path& models_root )
{
	std::vector< ModelConfig > models {};

	std::error_code error {};
	if ( !std::filesystem::is_directory( models_root, error ) )
	{
		spdlog::info( "Embedding models directory {} does not exist; no models loaded", models_root.string() );
		return models;
	}

	for ( const auto& entry : std::filesystem::directory_iterator { models_root, error } )
	{
		if ( !entry.is_directory() ) continue;

		auto config { readModelDirectory( entry.path() ) };
		if ( !config )
		{
			// Skipped, not fatal: one malformed model directory must not cost the worker every other
			// model in the same library.
			spdlog::warn( "Skipping embedding model at {}: {}", entry.path().string(), config.error() );
			continue;
		}

		spdlog::info(
			"Found embedding model '{}': {} dims, {}x{} input, {}",
			config->m_model_name,
			config->m_dimensions,
			config->m_preprocess.m_width,
			config->m_preprocess.m_height,
			config->m_text_onnx_path.empty() ? "image only" : "with text tower" );

		models.emplace_back( std::move( *config ) );
	}

	return models;
}

} // namespace premade
