#include "OnnxEmbedder.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <format>

using namespace idhan;

namespace premade
{

namespace
{

//! Vectors are compared by inner product, so a norm that drifts is a silently wrong result. fp16
//! storage moves a unit vector by roughly 1e-4, so the guard has to be looser than that to avoid
//! rejecting correct data, and tight enough to catch an export that forgot to normalise at all.
constexpr float NORM_TOLERANCE { 1e-3f };

} // namespace

OnnxEmbedder::OnnxEmbedder( ModuleCallbacks callbacks, ModelConfig config ) :
  EmbeddingModuleI( callbacks ),
  m_config( std::move( config ) )
{}

OnnxEmbedder::~OnnxEmbedder() = default;

void OnnxEmbedder::startup()
{
	m_env = std::make_unique< Ort::Env >( ORT_LOGGING_LEVEL_WARNING, "IDHANEmbedding" );

	Ort::SessionOptions options {};
	options.SetGraphOptimizationLevel( GraphOptimizationLevel::ORT_ENABLE_ALL );
	// One intra-op thread, and the parallelism comes from the worker's pool running several calls at
	// once instead. ORT's own pool would give a lone call more cores, but every other pool thread is
	// already running a pass of its own, so the two pools would multiply into an oversubscribed
	// machine that thrashes rather than a faster one.
	options.SetIntraOpNumThreads( 1 );

	m_session = std::make_unique< Ort::Session >( *m_env, m_config.m_onnx_path.c_str(), options );

	m_memory_info =
		std::make_unique< Ort::MemoryInfo >( Ort::MemoryInfo::CreateCpu( OrtArenaAllocator, OrtMemTypeDefault ) );

	// model.json is a claim about the graph; this is the check. Getting it wrong means writing
	// wrong-width vectors into a fixed-width halfvec column, so it fails startup rather than warning.
	const auto output_shape { m_session->GetOutputTypeInfo( 0 ).GetTensorTypeAndShapeInfo().GetShape() };

	if ( output_shape.empty() )
		throw std::runtime_error( std::format( "model '{}' exposes no output shape", m_config.m_model_name ) );

	const auto declared { static_cast< std::int64_t >( m_config.m_dimensions ) };

	if ( const auto actual { output_shape.back() }; actual > 0 && actual != declared )
		throw std::runtime_error(
			std::format(
				"model '{}' declares {} dimensions but its graph outputs {}",
				m_config.m_model_name,
				declared,
				actual ) );

	// Every pass is batch 1, so a graph frozen at 1 is exactly right and a dynamic axis is equally
	// fine. A graph frozen at anything larger is neither: it would reject our input on every single
	// call, so it fails startup rather than turning the whole backfill into a Run-time error per file.
	const auto input_shape { m_session->GetInputTypeInfo( 0 ).GetTensorTypeAndShapeInfo().GetShape() };

	if ( !input_shape.empty() && input_shape.front() > 1 )
		throw std::runtime_error(
			std::format(
				"model '{}' has a fixed batch dimension of {}; export it at batch 1 or with a dynamic batch axis",
				m_config.m_model_name,
				input_shape.front() ) );

	spdlog::info(
		"Embedding model '{}' ready: {} dims, {}x{} input",
		m_config.m_model_name,
		m_config.m_dimensions,
		m_config.m_preprocess.m_width,
		m_config.m_preprocess.m_height );

	if ( !supportsText() ) return;

	// Degraded rather than fatal throughout: a text tower that will not load costs text queries and
	// nothing else. Image embedding and record-reference search are unaffected, and a server that
	// refuses to start over an optional half is worse than one that says what it lost.
	auto tokenizer { BpeTokenizer::load( m_config.m_tokenizer_path, m_config.m_context_length ) };

	if ( !tokenizer )
	{
		m_text_failure = std::format( "tokenizer would not load: {}", tokenizer.error() );
		spdlog::error( "Model '{}': {}. Text queries will be refused", m_config.m_model_name, m_text_failure );
		return;
	}

	m_tokenizer = std::move( *tokenizer );

	try
	{
		Ort::SessionOptions text_options {};
		text_options.SetGraphOptimizationLevel( GraphOptimizationLevel::ORT_ENABLE_ALL );
		// Same reasoning as the image tower: parallelism comes from the worker's pool, and a second
		// wide pool here would oversubscribe rather than accelerate.
		text_options.SetIntraOpNumThreads( 1 );

		m_text_session = std::make_unique< Ort::Session >( *m_env, m_config.m_text_onnx_path.c_str(), text_options );
	}
	catch ( const Ort::Exception& e )
	{
		m_text_failure = std::format( "text graph would not load: {}", e.what() );
		spdlog::error( "Model '{}': {}. Text queries will be refused", m_config.m_model_name, m_text_failure );
		m_text_session.reset();
		return;
	}

	spdlog::info(
		"Embedding model '{}' text tower ready: context {}, input '{}', output '{}'",
		m_config.m_model_name,
		m_tokenizer.contextLength(),
		m_config.m_text_input_name,
		m_config.m_text_output_name );
}

void OnnxEmbedder::shutdown()
{
	// No handshake needed: the worker joins its pool threads before calling this, so no call is in
	// flight and there is nothing parked waiting to be woken.
	//
	// Sessions before the env: both hold it, and releasing what owns them first is how ORT is
	// documented to come down.
	m_text_session.reset();
	m_session.reset();
	m_memory_info.reset();
	m_env.reset();
}

std::vector< float > OnnxEmbedder::toTensor( const std::vector< std::byte >& pixels ) const
{
	const auto width { m_config.m_preprocess.m_width };
	const auto height { m_config.m_preprocess.m_height };
	const auto pixel_count { width * height };

	std::vector< float > tensor( pixel_count * 3, 0.0f );

	// Packed interleaved RGB in, planar CHW out. The host already colour-managed to sRGB and
	// flattened alpha, so this is the whole of the remaining preprocessing.
	for ( std::size_t index = 0; index < pixel_count; ++index )
	{
		for ( std::size_t channel = 0; channel < 3; ++channel )
		{
			const auto value {
				static_cast< float >( std::to_integer< std::uint8_t >( pixels[ index * 3 + channel ] ) ) / 255.0f
			};

			tensor[ channel * pixel_count + index ] = ( value - m_config.m_preprocess.m_mean[ channel ] )
			                                        / m_config.m_preprocess.m_std[ channel ];
		}
	}

	return tensor;
}

std::expected< std::vector< float >, ModuleError > OnnxEmbedder::runOne( std::vector< float >& tensor )
{
	const auto width { static_cast< std::int64_t >( m_config.m_preprocess.m_width ) };
	const auto height { static_cast< std::int64_t >( m_config.m_preprocess.m_height ) };

	try
	{
		const std::array< std::int64_t, 4 > shape { { 1, 3, height, width } };

		// Borrows `tensor` rather than copying it, so it has to outlive the Run below -- which is why
		// this takes a reference to the caller's vector instead of consuming it.
		auto input_tensor { Ort::Value::CreateTensor< float >(
			*m_memory_info, tensor.data(), tensor.size(), shape.data(), shape.size() ) };

		const std::array< const char*, 1 > input_names { { m_config.m_input_name.c_str() } };
		const std::array< const char*, 1 > output_names { { m_config.m_output_name.c_str() } };

		auto outputs {
			m_session->Run( Ort::RunOptions { nullptr }, input_names.data(), &input_tensor, 1, output_names.data(), 1 )
		};

		const float* const data { outputs.front().GetTensorData< float >() };

		std::vector< float > vector( data, data + m_config.m_dimensions );

		float sum { 0.0f };
		for ( const float value : vector ) sum += value * value;

		const float norm { std::sqrt( sum ) };

		if ( !m_config.m_normalized_output )
		{
			if ( norm > 0.0f )
				for ( float& value : vector ) value /= norm;

			return vector;
		}

		// The export is supposed to normalise. Checking is cheap next to a forward pass, and an
		// un-normalised vector reaching the index is not something a later stage detects.
		if ( std::abs( norm - 1.0f ) > NORM_TOLERANCE )
			return std::unexpected(
				ModuleError { std::format(
					"model '{}' claims normalized output but produced a vector of norm {}",
					m_config.m_model_name,
					norm ) } );

		return vector;
	}
	catch ( const Ort::Exception& e )
	{
		return std::unexpected( ModuleError { std::format( "onnxruntime failed: {}", e.what() ) } );
	}
}

std::expected< std::vector< float >, ModuleError > OnnxEmbedder::runText( std::vector< std::int64_t >& ids )
{
	try
	{
		const std::array< std::int64_t, 2 > shape { { 1, static_cast< std::int64_t >( ids.size() ) } };

		// Borrows `ids` rather than copying, so the caller's vector has to outlive the Run -- which is
		// why this takes a reference instead of consuming it, as runOne does.
		auto input_tensor { Ort::Value::CreateTensor< std::int64_t >(
			*m_memory_info, ids.data(), ids.size(), shape.data(), shape.size() ) };

		const std::array< const char*, 1 > input_names { { m_config.m_text_input_name.c_str() } };
		const std::array< const char*, 1 > output_names { { m_config.m_text_output_name.c_str() } };

		auto outputs { m_text_session->Run(
			Ort::RunOptions { nullptr }, input_names.data(), &input_tensor, 1, output_names.data(), 1 ) };

		const float* const data { outputs.front().GetTensorData< float >() };

		std::vector< float > vector( data, data + m_config.m_dimensions );

		float sum { 0.0f };
		for ( const float value : vector ) sum += value * value;

		const float norm { std::sqrt( sum ) };

		// Upstream ONNX exports return raw pooled features, so this is the normalisation rather than
		// a check on someone else's. A zero-magnitude result would divide by zero and produce NaNs
		// that only surface later as a NaN distance.
		if ( norm < NORM_TOLERANCE )
			return std::unexpected(
				ModuleError { std::format( "text tower for '{}' produced a zero vector", m_config.m_model_name ) } );

		if ( !m_config.m_normalized_output )
		{
			for ( float& value : vector ) value /= norm;
			return vector;
		}

		if ( std::abs( norm - 1.0f ) > NORM_TOLERANCE )
			return std::unexpected(
				ModuleError { std::format(
					"model '{}' claims normalized output but its text tower produced a vector of norm {}",
					m_config.m_model_name,
					norm ) } );

		return vector;
	}
	catch ( const Ort::Exception& e )
	{
		return std::unexpected( ModuleError { std::format( "onnxruntime failed on text: {}", e.what() ) } );
	}
}

std::expected< EmbeddingInfo, ModuleError > OnnxEmbedder::embedText( const std::string_view phrase )
{
	// supportsText() answered from disk before startup() ran, so reaching here with no session means
	// the tower was advertised and then failed to come up. The recorded reason is the useful part.
	if ( m_text_session == nullptr )
		return std::unexpected(
			ModuleError {
				m_text_failure.empty() ? std::string { "this model has no text encoder" } : m_text_failure } );

	auto ids { m_tokenizer.encode( phrase ) };

	auto vector { runText( ids ) };
	if ( !vector ) return std::unexpected( vector.error() );

	EmbeddingInfo info {};
	info.m_vector = std::move( *vector );

	return info;
}

std::expected< EmbeddingInfo, ModuleError > OnnxEmbedder::embed( ModuleCallData& data )
{
	if ( m_session == nullptr ) return std::unexpected( ModuleError { "embedding session is not initialised" } );

	if ( !m_callbacks.thumbnail ) return std::unexpected( ModuleError { "host provided no thumbnail callback" } );

	// Ask the host for exactly the geometry the model was trained on. Going through the callback
	// rather than decoding here is what gives this module every format the thumbnailer fleet covers
	// without linking a codec.
	Json::Value extra {};
	extra[ "width" ] = static_cast< Json::UInt64 >( m_config.m_preprocess.m_width );
	extra[ "height" ] = static_cast< Json::UInt64 >( m_config.m_preprocess.m_height );
	if ( m_config.m_preprocess.m_force_exact ) extra[ "resize_mode" ] = "force";

	const auto pixels { m_callbacks.thumbnail( data.file, extra, "" ) };
	if ( !pixels ) return std::unexpected( pixels.error() );

	const auto expected_bytes { m_config.m_preprocess.m_width * m_config.m_preprocess.m_height * 3 };

	if ( pixels->m_pixel_data.size() != expected_bytes )
		return std::unexpected(
			ModuleError { std::format(
				"host returned {} bytes of pixels for model '{}', expected {} ({}x{} RGB)",
				pixels->m_pixel_data.size(),
				m_config.m_model_name,
				expected_bytes,
				m_config.m_preprocess.m_width,
				m_config.m_preprocess.m_height ) } );

	auto tensor { toTensor( pixels->m_pixel_data ) };

	auto result { runOne( tensor ) };
	if ( !result ) return std::unexpected( result.error() );

	EmbeddingInfo info {};
	info.m_vector = std::move( *result );

	return info;
}

} // namespace premade
