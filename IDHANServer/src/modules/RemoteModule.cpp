//
// Created by kj16609 on 7/28/26.
//

#include "RemoteModule.hpp"

#include <algorithm>

#include "crypto/simpleHasher.hpp"

namespace idhan::modules
{

namespace
{

//! Copies a blob's bytes out into an owned buffer.
[[nodiscard]] std::vector< std::byte > toVector( const ipc::Blob& blob )
{
	const auto bytes { blob.bytes() };
	return std::vector< std::byte > { bytes.begin(), bytes.end() };
}

} // namespace

RemoteModule::RemoteModule(
	std::shared_ptr< WorkerPool > pool,
	const std::size_t module_index,
	std::string name,
	const ModuleType type,
	const ModuleVersion version,
	std::vector< std::string > mimes,
	std::string model_name,
	const std::uint32_t dimensions ) :
  m_pool( std::move( pool ) ),
  m_module_index( module_index ),
  m_name( std::move( name ) ),
  m_type( type ),
  m_version( version ),
  m_mimes( std::move( mimes ) ),
  m_model_name( std::move( model_name ) ),
  m_dimensions( dimensions )
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

} // namespace idhan::modules
