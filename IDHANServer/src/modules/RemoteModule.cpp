#include "RemoteModule.hpp"

#include <algorithm>

#include "crypto/simpleHasher.hpp"

namespace idhan::modules
{

//! Copies a blob's bytes out into an owned buffer.
[[nodiscard]] std::vector< std::byte > toVector( const ipc::Blob& blob )
{
	const auto bytes { blob.bytes() };
	return std::vector< std::byte > { bytes.begin(), bytes.end() };
}


RemoteModule::RemoteModule(
	std::shared_ptr< WorkerPool > pool,
	const std::size_t module_index,
	std::string name,
	const ModuleType type,
	const ModuleVersion version,
	std::vector< std::string > mimes,
	std::string model_name,
	const std::uint32_t dimensions,
	const bool supports_text ) :
  m_pool( std::move( pool ) ),
  m_module_index( module_index ),
  m_name( std::move( name ) ),
  m_type( type ),
  m_version( version ),
  m_mimes( std::move( mimes ) ),
  m_model_name( std::move( model_name ) ),
  m_dimensions( dimensions ),
  m_supports_text( supports_text )
{}

bool RemoteModule::canHandle( const std::string_view mime ) const
{
	return std::ranges::contains( m_mimes, mime );
}

Json::Value RemoteModule::baseBody( const ipc::CallOp op, const RemoteCallData& data ) const
{
	Json::Value body {};
	body[ ipc::field::TYPE ] = std::string { toString( ipc::MessageType::CALL ) };
	body[ ipc::field::OP ] = std::string { toString( op ) };
	body[ ipc::field::MODULE_INDEX ] = Json::UInt64 { m_module_index };
	body[ ipc::field::MIME ] = data.mime_name;
	body[ ipc::field::EXTRA ] = data.extra;
	body[ ipc::field::DEPTH ] = static_cast< Json::UInt >( data.depth );
	return body;
}

IDHANTask< std::expected< MetadataInfo, ModuleError > > RemoteModule::parseFile( RemoteCallData data ) const
{
	if ( data.input == nullptr ) co_return std::unexpected( ModuleError { "no input supplied" } );

	Json::Value body { baseBody( ipc::CallOp::METADATA, data ) };

	auto outcome { co_await m_pool->dispatch( std::move( body ), data.input ) };

	if ( !outcome->ok ) co_return std::unexpected( ModuleError { outcome->error } );

	auto info { ipc::metadataInfoFromJson( outcome->body[ ipc::field::METADATA ] ) };
	if ( !info ) co_return std::unexpected( ModuleError { info.error() } );

	co_return std::move( *info );
}

IDHANTask< std::expected< ThumbnailInfo, ModuleError > > RemoteModule::createThumbnailRaw(
	RemoteCallData data,
	const std::size_t width,
	const std::size_t height ) const
{
	if ( data.input == nullptr ) co_return std::unexpected( ModuleError { "no input supplied" } );

	Json::Value body { baseBody( ipc::CallOp::THUMB_RAW, data ) };
	body[ ipc::field::WIDTH ] = Json::UInt64 { width };
	body[ ipc::field::HEIGHT ] = Json::UInt64 { height };

	auto outcome { co_await m_pool->dispatch( std::move( body ), data.input ) };

	if ( !outcome->ok ) co_return std::unexpected( ModuleError { outcome->error } );

	auto thumbnail { ipc::thumbnailFromJson( outcome->body[ ipc::field::THUMBNAIL ], toVector( outcome->blob ) ) };
	if ( !thumbnail ) co_return std::unexpected( ModuleError { thumbnail.error() } );

	co_return std::move( *thumbnail );
}

IDHANTask< std::expected< ThumbnailInfo, ModuleError > > RemoteModule::createThumbnailFile(
	RemoteCallData data,
	const std::size_t width,
	const std::size_t height ) const
{
	if ( data.input == nullptr ) co_return std::unexpected( ModuleError { "no input supplied" } );

	Json::Value body { baseBody( ipc::CallOp::THUMB_FILE, data ) };
	body[ ipc::field::WIDTH ] = Json::UInt64 { width };
	body[ ipc::field::HEIGHT ] = Json::UInt64 { height };

	auto outcome { co_await m_pool->dispatch( std::move( body ), data.input ) };

	if ( !outcome->ok ) co_return std::unexpected( ModuleError { outcome->error } );

	auto thumbnail { ipc::thumbnailFromJson( outcome->body[ ipc::field::THUMBNAIL ], toVector( outcome->blob ) ) };
	if ( !thumbnail ) co_return std::unexpected( ModuleError { thumbnail.error() } );

	co_return std::move( *thumbnail );
}

IDHANTask< std::expected< ipc::Blob, ModuleError > > RemoteModule::generate(
	RemoteCallData data,
	const std::array< std::byte, 256 / 8 > desired_hash ) const
{
	if ( data.input == nullptr ) co_return std::unexpected( ModuleError { "no input supplied" } );

	Json::Value body { baseBody( ipc::CallOp::GENERATE, data ) };
	body[ ipc::field::HASH ] = crypto::toHex( desired_hash );

	auto outcome { co_await m_pool->dispatch( std::move( body ), data.input ) };

	if ( !outcome->ok ) co_return std::unexpected( ModuleError { outcome->error } );

	co_return std::move( outcome->blob );
}

IDHANTask< std::expected< EmbeddingInfo, ModuleError > > RemoteModule::embed( RemoteCallData data ) const
{
	if ( data.input == nullptr ) co_return std::unexpected( ModuleError { "no input supplied" } );

	Json::Value body { baseBody( ipc::CallOp::EMBED, data ) };

	auto outcome { co_await m_pool->dispatch( std::move( body ), data.input ) };

	if ( !outcome->ok ) co_return std::unexpected( ModuleError { outcome->error } );

	const auto& values { outcome->body[ ipc::field::EMBEDDING ] };
	if ( !values.isArray() ) co_return std::unexpected( ModuleError { "embed result carried no vector" } );

	// The worker already checks the width against what the module declares. Checking it again here
	// is not redundant: this side is what feeds a fixed-width halfvec column, and the two ends can
	// disagree if a library is rebuilt underneath a running server.
	if ( values.size() != m_dimensions )
		co_return std::unexpected(
			ModuleError { std::format(
				"embed result for model '{}' had {} values, expected {}",
				m_model_name,
				values.size(),
				m_dimensions ) } );

	EmbeddingInfo info {};
	info.m_vector.reserve( values.size() );

	for ( const auto& value : values )
	{
		if ( !value.isNumeric() ) co_return std::unexpected( ModuleError { "embed result had a non-numeric value" } );

		info.m_vector.push_back( static_cast< float >( value.asDouble() ) );
	}

	co_return info;
}

IDHANTask< std::expected< EmbeddingInfo, ModuleError > > RemoteModule::embedText( std::string phrase ) const
{
	if ( !m_supports_text ) co_return std::unexpected( ModuleError { "this model has no text encoder" } );

	// baseBody is not reused: it stamps MIME, extra and depth, all of which describe a file this
	// call does not have.
	Json::Value body {};
	body[ ipc::field::TYPE ] = std::string { toString( ipc::MessageType::CALL ) };
	body[ ipc::field::OP ] = std::string { toString( ipc::CallOp::EMBED_TEXT ) };
	body[ ipc::field::MODULE_INDEX ] = Json::UInt64 { m_module_index };
	body[ ipc::field::PHRASE ] = std::move( phrase );

	// Null input: the one call with no file to send. WorkerProcess::call skips the descriptor, the
	// size field, and the INPUT_REF registration for exactly this case.
	auto outcome { co_await m_pool->dispatch( std::move( body ), nullptr ) };

	if ( !outcome->ok ) co_return std::unexpected( ModuleError { outcome->error } );

	const auto& values { outcome->body[ ipc::field::EMBEDDING ] };
	if ( !values.isArray() ) co_return std::unexpected( ModuleError { "embed_text result carried no vector" } );

	// Checked again on this side for the same reason embed() does: this is the end that feeds a
	// fixed-width halfvec column, and the two ends can disagree if a library is rebuilt underneath
	// a running server.
	if ( values.size() != m_dimensions )
		co_return std::unexpected(
			ModuleError { std::format(
				"embed_text result for model '{}' had {} values, expected {}",
				m_model_name,
				values.size(),
				m_dimensions ) } );

	EmbeddingInfo info {};
	info.m_vector.reserve( values.size() );

	for ( const auto& value : values )
	{
		if ( !value.isNumeric() )
			co_return std::unexpected( ModuleError { "embed_text result had a non-numeric value" } );

		info.m_vector.push_back( static_cast< float >( value.asDouble() ) );
	}

	co_return info;
}

} // namespace idhan::modules
