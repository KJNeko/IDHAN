//
// Created by kj16609 on 8/10/26.
//

#include "ModelConfig.hpp"

#include <json/reader.h>
#include <json/value.h>
#include <spdlog/spdlog.h>

#include <fstream>
#include <sstream>

namespace premade
{

namespace
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

} // namespace

std::expected< ModelConfig, std::string > readModelConfig(
	const std::filesystem::path& config_path,
	const std::filesystem::path& onnx_path )
{
	std::ifstream file { config_path };
	if ( !file ) return std::unexpected( std::format( "could not open {}", config_path.string() ) );

	std::stringstream buffer {};
	buffer << file.rdbuf();

	Json::Value json {};
	Json::CharReaderBuilder builder {};
	std::string errors {};
	std::istringstream input { buffer.str() };

	if ( !Json::parseFromStream( builder, input, &json, &errors ) )
		return std::unexpected( std::format( "{} is not valid json: {}", config_path.string(), errors ) );

	if ( !json.isObject() ) return std::unexpected( std::format( "{} is not a json object", config_path.string() ) );

	ModelConfig config {};
	config.m_onnx_path = onnx_path;

	if ( !json[ "model_name" ].isString() )
		return std::unexpected( std::format( "{} has no string model_name", config_path.string() ) );

	config.m_model_name = json[ "model_name" ].asString();

	if ( !json[ "dimensions" ].isIntegral() || json[ "dimensions" ].asInt64() <= 0 )
		return std::unexpected( std::format( "{} has no positive integral dimensions", config_path.string() ) );

	config.m_dimensions = static_cast< std::size_t >( json[ "dimensions" ].asInt64() );

	// image_size is [width, height]. A single number is accepted as a square, which is what every
	// SigLIP2 variant actually uses.
	if ( const auto& size { json[ "image_size" ] }; size.isArray() && size.size() == 2 )
	{
		config.m_preprocess.m_width = static_cast< std::size_t >( size[ 0 ].asInt64() );
		config.m_preprocess.m_height = static_cast< std::size_t >( size[ 1 ].asInt64() );
	}
	else if ( size.isIntegral() )
	{
		config.m_preprocess.m_width = static_cast< std::size_t >( size.asInt64() );
		config.m_preprocess.m_height = config.m_preprocess.m_width;
	}

	// "squash" is open_clip's name for it; the host thumbnail callback calls the same thing "force".
	// Anything else (an aspect-preserving crop) is not something this module can ask the host for
	// today, so it is rejected rather than silently approximated -- a model given the wrong geometry
	// returns a plausible vector and no error.
	if ( const auto& mode { json[ "resize_mode" ] }; mode.isString() )
	{
		const auto value { mode.asString() };
		if ( value != "squash" )
			return std::unexpected(
				std::format(
					"{} requests resize_mode '{}'; only 'squash' is supported", config_path.string(), value ) );

		config.m_preprocess.m_force_exact = true;
	}

	readTriple( json[ "mean" ], config.m_preprocess.m_mean );
	readTriple( json[ "std" ], config.m_preprocess.m_std );

	if ( json[ "input_name" ].isString() ) config.m_input_name = json[ "input_name" ].asString();
	if ( json[ "output_name" ].isString() ) config.m_output_name = json[ "output_name" ].asString();
	if ( json[ "normalized_output" ].isBool() ) config.m_normalized_output = json[ "normalized_output" ].asBool();

	for ( const float value : config.m_preprocess.m_std )
	{
		if ( value == 0.0f )
			return std::unexpected(
				std::format( "{} has a zero in std, which would divide by zero", config_path.string() ) );
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

		const auto onnx_path { entry.path() / "model.onnx" };
		const auto config_path { entry.path() / "model.json" };

		if ( !std::filesystem::exists( onnx_path ) || !std::filesystem::exists( config_path ) ) continue;

		auto config { readModelConfig( config_path, onnx_path ) };
		if ( !config )
		{
			// Skipped, not fatal: one malformed model directory must not cost the worker every other
			// model in the same library.
			spdlog::warn( "Skipping embedding model at {}: {}", entry.path().string(), config.error() );
			continue;
		}

		spdlog::info(
			"Found embedding model '{}' ({} dims) at {}",
			config->m_model_name,
			config->m_dimensions,
			entry.path().string() );

		models.emplace_back( std::move( *config ) );
	}

	return models;
}

} // namespace premade
