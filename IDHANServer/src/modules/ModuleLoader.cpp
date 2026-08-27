#include "ModuleLoader.hpp"

#include <sys/prctl.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <mutex>
#include <optional>
#include <paths.hpp>
#include <string>
#include <thread>

#include "Config.hpp"
#include "MimeIDs.hpp"
#include "crypto/simpleHasher.hpp"
#include "drogon/HttpAppFramework.h"
#include "fgl/defines.hpp"
#include "logging/log.hpp"
#include "mime/prescan.hpp"

namespace idhan::modules
{

//! Size the host asks for when a module re-dispatches a thumbnail through the callbacks and does not
//! ask for a specific one.
constexpr std::size_t CALLBACK_THUMBNAIL_SIZE { 128 };

constexpr std::size_t MAX_CALLBACK_THUMBNAIL_SIZE { 8192 };

//! Reads a caller-requested thumbnail edge out of a callback's `extra`, clamped and defaulted.
[[nodiscard]] std::size_t requestedThumbnailEdge( const Json::Value& extra, const char* const key )
{
	if ( !extra.isObject() || !extra[ key ].isIntegral() ) return CALLBACK_THUMBNAIL_SIZE;

	const auto requested { extra[ key ].asInt64() };
	if ( requested < 1 ) return CALLBACK_THUMBNAIL_SIZE;

	return std::min( static_cast< std::size_t >( requested ), MAX_CALLBACK_THUMBNAIL_SIZE );
}

//! Calls one worker runs at once when the operator has not named a number.
constexpr std::size_t DEFAULT_POOL_THREADS { 4 };

//! Share of the machine the module system takes when the operator has not named a number.
/** The server itself is deliberately unrestricted; the modules are the part that has to be kept from
 *  taking the machine over, since libvips and the codec pools will each happily spin up one pool per
 *  core per call. */
constexpr std::size_t MODULE_THREAD_PERCENT { 25 };

//! Never zero, so it is safe to divide by.
[[nodiscard]] std::size_t hardwareThreads()
{
	return std::max( std::size_t { 1 }, static_cast< std::size_t >( std::thread::hardware_concurrency() ) );
}

//! Reads a worker setting, letting `[modules.<library>]` override the `[modules]` default.
/** \param library The library's file stem, e.g. IDHANVips. */
template < typename T >
[[nodiscard]] std::optional< T > moduleSetting( const std::string& library, const std::string_view name )
{
	if ( auto specific { config::get< T >( std::format( "modules.{}", library ), name ) } ) return specific;

	return config::get< T >( "modules", name );
}

//! \copydoc moduleSetting
template < typename T >
[[nodiscard]] T moduleSetting( const std::string& library, const std::string_view name, const T fallback )
{
	return moduleSetting< T >( library, name ).value_or( fallback );
}

//! A configured thread count, in which zero asks for every thread the machine has.
/** \return Nothing when the operator did not set it, leaving the caller to derive a default. */
[[nodiscard]] std::optional< std::size_t > threadSetting( const std::string& library, const std::string_view name )
{
	const auto configured { moduleSetting< std::size_t >( library, name ) };
	if ( !configured ) return std::nullopt;

	return *configured == 0 ? hardwareThreads() : *configured;
}

[[nodiscard]] WorkerSettings settingsFor( const std::filesystem::path& library )
{
	WorkerSettings settings {};

	const auto name { library.stem().string() };

	settings.library = library;
	settings.runner = getModuleRunnerPath();
	settings.pool_threads = threadSetting( name, "pool_threads" ).value_or( DEFAULT_POOL_THREADS );

	// Unset, the render budget is whatever is left of the library's share once the pool has been
	// divided out, so pool_threads * render_threads lands on that share rather than on the machine.
	const auto budget { std::max( std::size_t { 1 }, ( hardwareThreads() * MODULE_THREAD_PERCENT ) / 100 ) };

	settings.render_threads = threadSetting( name, "render_threads" )
	                              .value_or( std::max( std::size_t { 1 }, budget / settings.pool_threads ) );

	settings.heartbeat_interval = std::chrono::milliseconds {
		moduleSetting< std::size_t >( name, "heartbeat_interval_ms", 1000 )
	};
	settings.liveness_grace = std::chrono::milliseconds {
		moduleSetting< std::size_t >( name, "liveness_grace_ms", 5000 )
	};

	const auto level_name { spdlog::level::to_string_view( spdlog::get_level() ) };
	settings.log_level = std::string { level_name.data(), level_name.size() };

	return settings;
}

//! Cached: a callback resolves this, and config::get parses the config file on every read.
[[nodiscard]] std::uint32_t maxCallDepth()
{
	static std::uint32_t depth { 0 };
	static std::once_flag depth_once {};

	std::call_once(
		depth_once,
		[]()
		{ depth = static_cast< std::uint32_t >( config::get< std::size_t >( "modules", "max_call_depth", 4 ) ); } );

	return depth;
}

void publishEmbeddingModelPath()
{
	const auto path { config::get< std::string >( "embeddings", "model_path", std::string {} ) };

	if ( path.empty() ) return;

	if ( ::setenv( "IDHAN_EMBEDDING_MODELS", path.c_str(), 1 ) != 0 )
	{
		log::error( "Could not set IDHAN_EMBEDDING_MODELS to {}: {}", path, std::strerror( errno ) );
		return;
	}

	log::info( "Embedding models will be searched for in {}", path );
}

ModuleLoader::ModuleLoader()
{
	FGL_ASSERT( m_instance == nullptr, "ModuleLoader is a singleton" );
	m_instance = this;

	applyHardening();
	publishEmbeddingModelPath();
	loadModules();
}

void ModuleLoader::applyHardening()
{
#ifdef IDHAN_HARDEN
	if ( ::prctl( PR_SET_DUMPABLE, 0, 0, 0, 0 ) < 0 )
	{
		log::warn(
			"Could not disable ptrace of the server: {}. Module workers can attach to it.", std::strerror( errno ) );
		return;
	}

	log::info( "Hardening active (IDHAN_HARDEN): server is not ptrace-able, worker core dumps disabled" );
#endif
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
			log::warn( "Skipping module library {}: {}", path.string(), manifest.error() );
			interrogator->terminate( "interrogation failed" );
			return false;
		}

		entries = std::move( *manifest );
		interrogator->terminate( "interrogation finished", Termination::EXPECTED );
	}

	if ( entries.empty() )
	{
		log::warn( "Skipping module library {}: it exports no modules", path.string() );
		return false;
	}

	const bool persistent { std::ranges::any_of(
		entries, []( const auto& entry ) { return entry.residency == ModuleResidency::PERSISTENT; } ) };

	std::size_t declared_ceiling_mb { 0 };
	for ( const auto& entry : entries ) declared_ceiling_mb = std::max( declared_ceiling_mb, entry.rss_ceiling_mb );

	const auto library_name { path.stem().string() };

	const auto configured_ceiling_mb { moduleSetting< std::size_t >( library_name, "rss_limit_mb", 2048 ) };
	const auto ceiling_mb { std::max( configured_ceiling_mb, declared_ceiling_mb ) };

	if ( declared_ceiling_mb > configured_ceiling_mb )
		log::info(
			"Module library {} declares a resident set of {} MiB, above the configured {} MiB ceiling; its worker "
			"is bounded at {} MiB",
			path.string(),
			declared_ceiling_mb,
			configured_ceiling_mb,
			ceiling_mb );

	auto settings { settingsFor( path ) };
	settings.expected_signature = ipc::manifestSignature( entries );

	// A worker's budget is pool_threads * render_threads either way. When nothing in the library can
	// use a render thread inside a call, that budget buys concurrency instead of going unspent.
	// all_of, not any_of: the modules share one process, so one that does parallelise would end up
	// with both the multiplied pool and its own render threads.
	const bool folds_render_threads {
		std::ranges::all_of( entries, []( const auto& entry ) { return entry.single_threaded; } )
	};

	if ( folds_render_threads && settings.render_threads > 1 )
	{
		settings.pool_threads *= settings.render_threads;
		settings.render_threads = 1;
	}

	log::info(
		"Module library {} runs {} concurrent call(s), each allowed {} render thread(s){}",
		path.filename().string(),
		settings.pool_threads,
		settings.render_threads,
		folds_render_threads ? " (single-threaded: the render budget went to the pool)" : "" );

	const auto library_index { m_pools.size() };

	auto pool { std::make_shared< WorkerPool >(
		settings,
		persistent ? ModuleResidency::PERSISTENT : ModuleResidency::SINGLE_RUN,
		ceiling_mb * 1024,
		std::chrono::seconds { moduleSetting< std::size_t >( library_name, "idle_timeout_sec", 300 ) },
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
				.single_threaded = entry.single_threaded,
				.residency = entry.residency,
				.mimes = entry.mimes,
				.model_name = entry.model_name,
				.dimensions = entry.dimensions } );

		const auto slot { m_modules.size() };

		m_modules.emplace_back(
			std::make_shared< RemoteModule >(
				pool,
				entry.index,
				entry.name,
				entry.type,
				entry.version,
				entry.mimes,
				entry.model_name,
				entry.dimensions,
				entry.supports_text ) );

		if ( ( entry.type & ModuleTypeFlags::EMBEDDING ) != 0 )
		{
			if ( const auto [ it, inserted ] { m_by_model_embedding.try_emplace( entry.model_name, slot ) }; !inserted )
			{
				log::warn(
					"Module '{}' from {} claims model '{}', which is already provided by module '{}'. Ignoring the "
					"duplicate.",
					entry.name,
					path.filename().string(),
					entry.model_name,
					m_descriptors[ it->second ].name );
			}
		}

		for ( const auto mime_id : entry.mimes )
		{
			if ( ( entry.type & ModuleTypeFlags::METADATA ) != 0 ) m_by_mime_metadata[ mime_id ].emplace_back( slot );
			if ( ( entry.type & ModuleTypeFlags::THUMBNAILER ) != 0 )
				m_by_mime_thumbnailer[ mime_id ].emplace_back( slot );
			if ( ( entry.type & ModuleTypeFlags::GENERATOR ) != 0 ) m_by_mime_generator[ mime_id ].emplace_back( slot );
			if ( ( entry.type & ModuleTypeFlags::MIME_PARSE ) != 0 ) m_by_mime_parser[ mime_id ].emplace_back( slot );
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
	m_by_mime_parser.clear();
	m_modules.clear();
	m_descriptors.clear();
	m_pools.clear();
}

void ModuleLoader::maintainWorkers()
{
	for ( const auto& pool : m_pools ) pool->maintain();
}

std::vector< std::shared_ptr< RemoteModule > > ModuleLoader::lookup(
	const std::unordered_map< MimeID, std::vector< std::size_t > >& index,
	const MimeID mime_id ) const
{
	std::vector< std::shared_ptr< RemoteModule > > modules {};

	const auto found { index.find( mime_id ) };
	if ( found == index.end() ) return modules;

	modules.reserve( found->second.size() );
	for ( const auto slot : found->second ) modules.emplace_back( m_modules[ slot ] );

	return modules;
}

std::vector< std::shared_ptr< RemoteModule > > ModuleLoader::getThumbnailerFor( const MimeID mime_id ) const
{
	return lookup( m_by_mime_thumbnailer, mime_id );
}

std::vector< std::shared_ptr< RemoteModule > > ModuleLoader::getParserFor( const MimeID mime_id ) const
{
	return lookup( m_by_mime_metadata, mime_id );
}

std::shared_ptr< RemoteModule > ModuleLoader::getEmbedderFor( const std::string_view model_name ) const
{
	const auto found { m_by_model_embedding.find( std::string { model_name } ) };
	if ( found == m_by_model_embedding.end() ) return nullptr;

	return m_modules[ found->second ];
}

std::vector< std::pair< std::string, std::uint32_t > > ModuleLoader::embeddingModels() const
{
	std::vector< std::pair< std::string, std::uint32_t > > models {};
	models.reserve( m_by_model_embedding.size() );

	for ( const auto& [ model_name, slot ] : m_by_model_embedding )
		models.emplace_back( model_name, m_descriptors[ slot ].dimensions );

	return models;
}

std::vector< std::shared_ptr< RemoteModule > > ModuleLoader::getGeneratorsFor( const MimeID mime_id ) const
{
	return lookup( m_by_mime_generator, mime_id );
}

std::vector< std::shared_ptr< RemoteModule > > ModuleLoader::getMimeParserFor( const MimeID mime_id ) const
{
	return lookup( m_by_mime_parser, mime_id );
}

using ExpectedInput = std::expected< std::shared_ptr< const CallInput >, std::string >;

//! Wraps a descriptor a module sent with its callback as an input for the nested call.
[[nodiscard]] ExpectedInput makeCallbackInput( ipc::UniqueFd fd )
{
	auto blob { ipc::Blob::adoptSealed( std::move( fd ) ) };
	if ( !blob ) return std::unexpected( blob.error() );

	auto input { CallInput::forBlob( std::move( *blob ) ) };
	if ( !input ) return std::unexpected( input.error() );

	return std::make_shared< const CallInput >( std::move( *input ) );
}

drogon::Task< void > runCallback( std::shared_ptr< WorkerProcess > worker, ipc::Frame frame )
{
	const std::uint64_t callback_id { frame.body[ ipc::field::CALLBACK_ID ].asUInt64() };
	const auto kind { ipc::fromWire< ipc::CallbackKind >( frame.body[ ipc::field::KIND ] ) };
	const std::uint32_t depth { frame.body[ ipc::field::DEPTH ].asUInt() };

	Json::Value reply {};
	reply[ ipc::field::TYPE ] = ipc::toWire( ipc::MessageType::CALLBACK_RESULT );
	reply[ ipc::field::CALLBACK_ID ] = Json::UInt64 { callback_id };
	reply[ ipc::field::OK ] = false;
	reply[ ipc::field::ERROR ] = "";

	std::vector< int > reply_fds {};
	//! Kept alive until the reply has been queued: FrameWriter dups the descriptor when it takes the
	//! frame, but not before.
	ipc::Blob produced {};

	const InFlightInput referenced {
		frame.body[ ipc::field::INPUT_REF ].isIntegral() ?
			worker->inputForCall( frame.body[ ipc::field::INPUT_REF ].asUInt64() ) :
			InFlightInput {}
	};

	const bool by_reference { referenced.input != nullptr };

	if ( !kind )
	{
		reply[ ipc::field::ERROR ] = "unknown callback kind";
	}
	else if ( *kind == ipc::CallbackKind::GENERATE && !frame.body[ ipc::field::HASH ].isString() )
	{
		reply[ ipc::field::ERROR ] = "generate callback did not carry a sha256 hash string";
	}
	else if ( !by_reference && frame.fds.empty() )
	{
		reply[ ipc::field::ERROR ] = "callback arrived without its payload";
	}
	else if ( depth + 1 > maxCallDepth() )
	{
		reply[ ipc::field::ERROR ] = std::format( "module call nesting exceeded the limit of {}", maxCallDepth() );
	}
	else if ( auto input { by_reference ? ExpectedInput { referenced.input } :
	                                      makeCallbackInput( std::move( frame.fds.front() ) ) };
	          !input )
	{
		reply[ ipc::field::ERROR ] = input.error();
	}
	else if ( const auto detected {
				  by_reference ? referenced.mime_id :
								 co_await mime::prescanMime( mime::MimeReader { ( *input )->blob().view() } ) };
	          detected == mime_ids::UNKNOWN )
	{
		reply[ ipc::field::ERROR ] = "could not determine the mime type of the callback payload";
	}
	else
	{
		auto& loader { ModuleLoader::instance() };

		RemoteCallData data {
			.input = *input, .mime_id = detected, .extra = frame.body[ ipc::field::EXTRA ], .depth = depth + 1
		};

		switch ( *kind )
		{
			case ipc::CallbackKind::PROBE:
				{
					ModuleCapability capability {};
					capability.mime_id = detected;
					capability.has_metadata = !loader.getParserFor( detected ).empty();
					capability.has_thumbnailer = !loader.getThumbnailerFor( detected ).empty();
					capability.has_generator = !loader.getGeneratorsFor( detected ).empty();

					reply[ ipc::field::CAPABILITY ] = ipc::toJson( capability );
					reply[ ipc::field::OK ] = true;
					break;
				}
			case ipc::CallbackKind::THUMBNAIL:
				{
					const auto thumbnailers { loader.getThumbnailerFor( detected ) };
					if ( thumbnailers.empty() )
					{
						reply[ ipc::field::ERROR ] = std::format( "no thumbnailer for mime type {}", detected );
						break;
					}

					// Read before the move: `data` owns the extra the caller sent.
					const auto width { requestedThumbnailEdge( data.extra, ipc::field::WIDTH ) };
					const auto height { requestedThumbnailEdge( data.extra, ipc::field::HEIGHT ) };

					auto thumbnail {
						co_await thumbnailers.front()->createThumbnailRaw( std::move( data ), width, height )
					};

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
					const auto generators { loader.getGeneratorsFor( detected ) };
					if ( generators.empty() )
					{
						reply[ ipc::field::ERROR ] = std::format( "no generator for mime type {}", detected );
						break;
					}

					const auto hex { frame.body[ ipc::field::HASH ].asString() };
					if ( hex.size() != ( 256 / 8 ) * 2 )
					{
						reply[ ipc::field::ERROR ] = "generate callback did not carry a sha256 hash";
						break;
					}

					auto generated {
						co_await generators.front()->generate( std::move( data ), crypto::fromHex( hex ) )
					};

					if ( !generated )
					{
						reply[ ipc::field::ERROR ] = generated.error();
						break;
					}

					produced = std::move( *generated );
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

void ModuleLoader::serviceCallback( std::shared_ptr< WorkerProcess > worker, ipc::Frame frame )
{
	static std::atomic< std::size_t > next_loop { 0 };

	auto* const loop { drogon::app().getIOLoop( next_loop.fetch_add( 1 ) ) };

	auto payload { std::make_shared< std::pair< std::shared_ptr< WorkerProcess >, ipc::Frame > >(
		std::move( worker ), std::move( frame ) ) };

	auto body = [ payload ]() mutable
	{
		drogon::async_run(
			[ payload ]() mutable -> drogon::Task< void >
			{ co_await runCallback( std::move( payload->first ), std::move( payload->second ) ); } );
	};

	( loop != nullptr ? loop : drogon::app().getLoop() )->queueInLoop( std::move( body ) );
}

} // namespace idhan::modules
