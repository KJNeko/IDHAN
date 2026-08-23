#include "RemoteModule.hpp"

#include <algorithm>
#include <format>

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
	std::vector< MimeID > mimes,
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

bool RemoteModule::canHandle( const MimeID mime_id ) const
{
	return std::ranges::contains( m_mimes, mime_id );
}

Json::Value RemoteModule::baseBody( const ipc::CallOp op, const RemoteCallData& data ) const
{
	Json::Value body {};
	body[ ipc::field::TYPE ] = ipc::toWire( ipc::MessageType::CALL );
	body[ ipc::field::OP ] = ipc::toWire( op );
	body[ ipc::field::MODULE_INDEX ] = Json::UInt64 { m_module_index };
	body[ ipc::field::MIME_ID ] = static_cast< Json::Int >( data.mime_id );
	body[ ipc::field::EXTRA ] = data.extra;
	body[ ipc::field::DEPTH ] = static_cast< Json::UInt >( data.depth );
	return body;
}

IDHANTask< std::expected< MetadataInfo, ModuleError > > RemoteModule::parseFile( RemoteCallData data ) const
{
	if ( data.input == nullptr ) co_return std::unexpected( ModuleError { "no input supplied" } );

	Json::Value body { baseBody( ipc::CallOp::METADATA, data ) };

	auto outcome { co_await m_pool->dispatch( std::move( body ), data.input, data.depth != SERVER_ORIGINATED ) };

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

	Json::Value body { baseBody( ipc::CallOp::THUMB_RGBA, data ) };
	body[ ipc::field::WIDTH ] = Json::UInt64 { width };
	body[ ipc::field::HEIGHT ] = Json::UInt64 { height };

	auto outcome { co_await m_pool->dispatch( std::move( body ), data.input, data.depth != SERVER_ORIGINATED ) };

	if ( !outcome->ok ) co_return std::unexpected( ModuleError { outcome->error } );
	if ( !outcome->blob.valid() ) co_return std::unexpected( ModuleError { "thumbnail result carried no payload" } );

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

	auto outcome { co_await m_pool->dispatch( std::move( body ), data.input, data.depth != SERVER_ORIGINATED ) };

	if ( !outcome->ok ) co_return std::unexpected( ModuleError { outcome->error } );
	if ( !outcome->blob.valid() ) co_return std::unexpected( ModuleError { "thumbnail result carried no payload" } );

	auto thumbnail { ipc::thumbnailFromJson( outcome->body[ ipc::field::THUMBNAIL ], toVector( outcome->blob ) ) };
	if ( !thumbnail ) co_return std::unexpected( ModuleError { thumbnail.error() } );

	co_return std::move( *thumbnail );
}

IDHANTask< std::expected< MimeID, ModuleError > > RemoteModule::parseMime( RemoteCallData data ) const
{
	if ( data.input == nullptr ) co_return std::unexpected( ModuleError { "no input supplied" } );

	Json::Value body { baseBody( ipc::CallOp::MIME_PARSE, data ) };

	auto outcome { co_await m_pool->dispatch( std::move( body ), data.input, data.depth != SERVER_ORIGINATED ) };

	if ( !outcome->ok ) co_return std::unexpected( ModuleError { outcome->error } );

	const auto& mime_id { outcome->body[ ipc::field::MIME_ID ] };

	if ( !mime_id.isIntegral() )
		co_return std::unexpected(
			ModuleError { std::format( "mime parser answered with {}", ipc::describeWireValue( mime_id ) ) } );

	co_return static_cast< MimeID >( mime_id.asInt() );
}

IDHANTask< std::expected< ipc::Blob, ModuleError > > RemoteModule::generate(
	RemoteCallData data,
	const std::array< std::byte, 256 / 8 > desired_hash ) const
{
	if ( data.input == nullptr ) co_return std::unexpected( ModuleError { "no input supplied" } );

	Json::Value body { baseBody( ipc::CallOp::GENERATE, data ) };
	body[ ipc::field::HASH ] = crypto::toHex( desired_hash );

	auto outcome { co_await m_pool->dispatch( std::move( body ), data.input, data.depth != SERVER_ORIGINATED ) };

	if ( !outcome->ok ) co_return std::unexpected( ModuleError { outcome->error } );
	if ( !outcome->blob.valid() ) co_return std::unexpected( ModuleError { "generate result carried no payload" } );

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

	Json::Value body {};
	body[ ipc::field::TYPE ] = ipc::toWire( ipc::MessageType::CALL );
	body[ ipc::field::OP ] = ipc::toWire( ipc::CallOp::EMBED_TEXT );
	body[ ipc::field::MODULE_INDEX ] = Json::UInt64 { m_module_index };
	body[ ipc::field::PHRASE ] = std::move( phrase );

	auto outcome { co_await m_pool->dispatch( std::move( body ), nullptr ) };

	if ( !outcome->ok ) co_return std::unexpected( ModuleError { outcome->error } );

	const auto& values { outcome->body[ ipc::field::EMBEDDING ] };
	if ( !values.isArray() ) co_return std::unexpected( ModuleError { "embed_text result carried no vector" } );

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
