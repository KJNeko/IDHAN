//
// Created by kj16609 on 6/11/25.
//
#include "ModuleLoader.hpp"

#include <algorithm>
#include <filesystem>
#include <format>

#include <paths.hpp>

#include "Config.hpp"
#include "crypto/simpleHasher.hpp"
#include "drogon/HttpAppFramework.h"
#include "fgl/defines.hpp"
#include "logging/log.hpp"
#include "mime/MimeDatabase.hpp"

namespace idhan::modules
{

namespace
{

//! Size the host asks for when a module re-dispatches a thumbnail through the callbacks.
/** Fixed because ModuleCallbacks::thumbnail has no size parameter -- a module asking the host to
 *  thumbnail some bytes has no way to say how big it wants the result. Making it caller-specified
 *  would mean widening that signature, which is a module-ABI change; until then the archive
 *  thumbnailer (the only caller) gets tiles at this size to composite. */
constexpr std::size_t CALLBACK_THUMBNAIL_SIZE { 128 };

[[nodiscard]] WorkerSettings settingsFor( const std::filesystem::path& library )
{
	WorkerSettings settings {};

	settings.library = library;
	settings.runner = getModuleRunnerPath();
	settings.pool_threads = config::get< std::size_t >( "modules", "pool_threads", 4 );
	settings.heartbeat_interval = std::chrono::milliseconds {
		config::get< std::size_t >( "modules", "heartbeat_interval_ms", 1000 )
	};
	settings.liveness_grace = std::chrono::milliseconds {
		config::get< std::size_t >( "modules", "liveness_grace_ms", 5000 )
	};
	settings.timeout_multiplier =
		static_cast< double >( config::get< std::size_t >( "modules", "timeout_multiplier", 4 ) );
	settings.max_timeout = std::chrono::seconds { config::get< std::size_t >( "modules", "max_timeout_sec", 600 ) };

	return settings;
}

[[nodiscard]] std::uint32_t maxCallDepth()
{
	return static_cast< std::uint32_t >( config::get< std::size_t >( "modules", "max_call_depth", 4 ) );
}

} // namespace

ModuleLoader::ModuleLoader()
{
	FGL_ASSERT( m_instance == nullptr, "ModuleLoader is a singleton" );
	m_instance = this;
	loadModules();
}

ModuleLoader::~ModuleLoader()
{
	unloadModules();
	m_instance = nullptr;
}

bool ModuleLoader::registerLibrary( const std::filesystem::path& path )
{
	std::vector< ipc::ManifestEntry > entries {};

	{
		// A throwaway process whose only job is to say what the library exports. --describe skips the
		// library's init(), so enumerating modules never pays VIPS_INIT.
		auto settings { settingsFor( path ) };
		settings.describe_only = true;

		const auto interrogator { std::make_shared< WorkerProcess >( settings, nullptr ) };

		if ( const auto started { interrogator->start() }; !started )
		{
			log::warn( "Skipping module library {}: {}", path.string(), started.error() );
			return false;
		}

		auto manifest { interrogator->awaitManifest(
			std::chrono::seconds { config::get< std::size_t >( "modules", "describe_timeout_sec", 10 ) } ) };

		if ( !manifest )
		{
			// Deliberately not fatal. The previous in-process loader called std::abort() here, so a
			// single unusable third-party .so stopped the server from starting at all.
			log::warn( "Skipping module library {}: {}", path.string(), manifest.error() );
			interrogator->terminate( "interrogation failed" );
			return false;
		}

		entries = std::move( *manifest );
		interrogator->terminate( "interrogation finished" );
	}

	if ( entries.empty() )
	{
		log::warn( "Skipping module library {}: it exports no modules", path.string() );
		return false;
	}

	// A library is persistent if any one of its modules asked to be: residency is declared per
	// module, but the process hosts the whole library, so the longest-lived request wins.
	const bool persistent { std::ranges::any_of(
		entries, []( const auto& entry ) { return entry.residency == ModuleResidency::PERSISTENT; } ) };

	auto settings { settingsFor( path ) };
	settings.expected_signature = ipc::manifestSignature( entries );

	const auto library_index { m_pools.size() };

	auto pool { std::make_shared< WorkerPool >(
		settings,
		persistent ? ModuleResidency::PERSISTENT : ModuleResidency::SINGLE_RUN,
		config::get< std::size_t >( "modules", "rss_limit_mb", 2048 ) * 1024,
		std::chrono::seconds { config::get< std::size_t >( "modules", "idle_timeout_sec", 300 ) },
		[ this ]( std::shared_ptr< WorkerProcess > worker, ipc::Frame frame )
		{ serviceCallback( std::move( worker ), std::move( frame ) ); } ) };

	m_pools.emplace_back( pool );

	for ( const auto& entry : entries )
	{
		m_descriptors.emplace_back(
			ModuleDescriptor {
				.library_index = library_index,
				.module_index = entry.index,
				.name = entry.name,
				.type = entry.type,
				.version = entry.version,
				.thread_safe = entry.thread_safe,
				.residency = entry.residency,
				.mimes = entry.mimes } );

		const auto slot { m_modules.size() };

		m_modules.emplace_back(
			std::make_shared< RemoteModule >( pool, entry.index, entry.name, entry.type, entry.version, entry.mimes ) );

		for ( const auto& mime : entry.mimes )
		{
			if ( ( entry.type & ModuleTypeFlags::METADATA ) != 0 ) m_by_mime_metadata[ mime ].emplace_back( slot );
			if ( ( entry.type & ModuleTypeFlags::THUMBNAILER ) != 0 )
				m_by_mime_thumbnailer[ mime ].emplace_back( slot );
			if ( ( entry.type & ModuleTypeFlags::GENERATOR ) != 0 ) m_by_mime_generator[ mime ].emplace_back( slot );
		}

		log::info(
			"Registered module '{}' from {} [index {}, {} mimes, {}]",
			entry.name,
			path.filename().string(),
			entry.index,
			entry.mimes.size(),
			ipc::toString( entry.residency ) );
	}

	pool->prewarm();

	return true;
}

void ModuleLoader::loadModules()
{
	for ( const auto& path : getModulePaths() )
	{
		if ( !std::filesystem::is_regular_file( path ) ) continue;
		[[maybe_unused]] const auto registered { registerLibrary( path ) };
	}

	log::info( "Module system ready: {} modules across {} libraries", m_modules.size(), m_pools.size() );
}

void ModuleLoader::unloadModules()
{
	for ( const auto& pool : m_pools ) pool->shutdown();

	m_by_mime_metadata.clear();
	m_by_mime_thumbnailer.clear();
	m_by_mime_generator.clear();
	m_modules.clear();
	m_descriptors.clear();
	m_pools.clear();
}

void ModuleLoader::maintainWorkers()
{
	for ( const auto& pool : m_pools ) pool->maintain();
}

std::vector< std::shared_ptr< RemoteModule > > ModuleLoader::lookup(
	const std::unordered_map< std::string, std::vector< std::size_t > >& index,
	const std::string_view mime ) const
{
	std::vector< std::shared_ptr< RemoteModule > > modules {};

	const auto found { index.find( std::string { mime } ) };
	if ( found == index.end() ) return modules;

	modules.reserve( found->second.size() );
	for ( const auto slot : found->second ) modules.emplace_back( m_modules[ slot ] );

	return modules;
}

std::vector< std::shared_ptr< RemoteModule > > ModuleLoader::getThumbnailerFor( const std::string_view mime ) const
{
	return lookup( m_by_mime_thumbnailer, mime );
}

std::vector< std::shared_ptr< RemoteModule > > ModuleLoader::getParserFor( const std::string_view mime ) const
{
	return lookup( m_by_mime_metadata, mime );
}

std::vector< std::shared_ptr< RemoteModule > > ModuleLoader::getGeneratorsFor( const std::string_view mime ) const
{
	return lookup( m_by_mime_generator, mime );
}

//! Does the work behind one CALLBACK frame.
/** A free coroutine taking everything by value: parameters are copied into the coroutine frame,
 *  whereas a capturing lambda's closure is not, and IDHANTask/drogon::Task are lazy enough that the
 *  distinction is the difference between working code and a use-after-free. */
namespace
{

drogon::Task< void > runCallback( std::shared_ptr< WorkerProcess > worker, ipc::Frame frame )
{
	const std::uint64_t callback_id { frame.body[ ipc::field::CALLBACK_ID ].asUInt64() };
	const auto kind { ipc::callbackKindFromString( frame.body[ ipc::field::KIND ].asString() ) };
	const std::uint32_t depth { frame.body[ ipc::field::DEPTH ].asUInt() };
	const auto file_name { frame.body[ ipc::field::FILE_NAME ].asString() };

	Json::Value reply {};
	reply[ ipc::field::TYPE ] = std::string { toString( ipc::MessageType::CALLBACK_RESULT ) };
	reply[ ipc::field::CALLBACK_ID ] = Json::UInt64 { callback_id };
	reply[ ipc::field::OK ] = false;
	reply[ ipc::field::ERROR ] = "";

	std::vector< int > reply_fds {};
	//! Kept alive until the reply has been queued: FrameWriter dups the descriptor when it takes the
	//! frame, but not before.
	ipc::Blob produced {};

	if ( !kind )
	{
		reply[ ipc::field::ERROR ] = "unknown callback kind";
	}
	else if ( frame.fds.empty() )
	{
		reply[ ipc::field::ERROR ] = "callback arrived without its payload";
	}
	else if ( depth + 1 > maxCallDepth() )
	{
		// Enforced in the host because the host is the only party that sees the whole chain: the
		// recursion crosses process boundaries, so no per-process counter can bound it.
		reply[ ipc::field::ERROR ] = std::format( "module call nesting exceeded the limit of {}", maxCallDepth() );
	}
	else if ( auto payload { ipc::Blob::adopt( std::move( frame.fds.front() ) ) }; !payload )
	{
		reply[ ipc::field::ERROR ] = payload.error();
	}
	else if ( const auto detected { co_await getMimeDatabase()->scan( payload->view(), file_name ) }; !detected )
	{
		reply[ ipc::field::ERROR ] = "could not determine the mime type of the callback payload";
	}
	else
	{
		auto& loader { ModuleLoader::instance() };

		RemoteCallData data {
			.blob = &payload.value(),
			.mime_name = *detected,
			.extra = frame.body[ ipc::field::EXTRA ],
			.depth = depth + 1
		};

		switch ( *kind )
		{
			case ipc::CallbackKind::PROBE:
				{
					ModuleCapability capability {};
					capability.mime = *detected;
					capability.has_metadata = !loader.getParserFor( *detected ).empty();
					capability.has_thumbnailer = !loader.getThumbnailerFor( *detected ).empty();
					capability.has_generator = !loader.getGeneratorsFor( *detected ).empty();

					reply[ ipc::field::CAPABILITY ] = ipc::toJson( capability );
				reply[ ipc::field::OK ] = true;
				break;
			}
			case ipc::CallbackKind::THUMBNAIL:
			{
				const auto thumbnailers { loader.getThumbnailerFor( *detected ) };
				if ( thumbnailers.empty() )
				{
					reply[ ipc::field::ERROR ] = std::format( "no thumbnailer for mime type {}", *detected );
					break;
				}

				auto thumbnail { co_await thumbnailers.front()->createThumbnailRaw(
					std::move( data ), CALLBACK_THUMBNAIL_SIZE, CALLBACK_THUMBNAIL_SIZE ) };

				if ( !thumbnail )
				{
					reply[ ipc::field::ERROR ] = thumbnail.error();
					break;
				}

				auto blob { ipc::Blob::fromBytes( thumbnail->m_pixel_data ) };
				if ( !blob )
				{
					reply[ ipc::field::ERROR ] = blob.error();
					break;
				}

				produced = std::move( *blob );
				reply_fds.emplace_back( produced.fd() );
				reply[ ipc::field::THUMBNAIL ] = ipc::thumbnailHeaderToJson( *thumbnail );
				reply[ ipc::field::OK ] = true;
				break;
			}
			case ipc::CallbackKind::GENERATE:
				{
					const auto generators { loader.getGeneratorsFor( *detected ) };
					if ( generators.empty() )
					{
						reply[ ipc::field::ERROR ] = std::format( "no generator for mime type {}", *detected );
						break;
					}

					const auto hex { frame.body[ ipc::field::HASH ].asString() };
				if ( hex.size() != ( 256 / 8 ) * 2 )
				{
					reply[ ipc::field::ERROR ] = "generate callback did not carry a sha256 hash";
					break;
				}

				auto generated { co_await generators.front()->generate( std::move( data ), crypto::fromHex( hex ) ) };

				if ( !generated )
				{
					reply[ ipc::field::ERROR ] = generated.error();
					break;
				}

				auto blob { ipc::Blob::fromBytes( *generated ) };
				if ( !blob )
				{
					reply[ ipc::field::ERROR ] = blob.error();
					break;
				}

				produced = std::move( *blob );
				reply_fds.emplace_back( produced.fd() );
				reply[ ipc::field::OK ] = true;
				break;
				}
		}
	}

	if ( const auto posted { worker->post( reply, reply_fds ) }; !posted )
		log::warn( "Could not answer module callback {}: {}", callback_id, posted.error() );

	co_return;
}

} // namespace

void ModuleLoader::serviceCallback( std::shared_ptr< WorkerProcess > worker, ipc::Frame frame )
{
	// Runs on a drogon loop, never on the worker's IO thread. Servicing a callback means a MIME scan
	// and usually a call into another worker; the IO thread has to stay free to keep reading, because
	// the module that raised this callback is blocked waiting for the answer to come back down it.
	drogon::async_run(
		[ worker = std::move( worker ), frame = std::move( frame ) ]() mutable -> drogon::Task< void >
		{ co_await runCallback( std::move( worker ), std::move( frame ) ); } );
}

} // namespace idhan::modules
