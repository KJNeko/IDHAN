//
// Created by kj16609 on 6/11/25.
//
#include "ModuleLoader.hpp"

#ifdef __linux__
#include <dlfcn.h>
#elif defined( _WIN32 )
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <filesystem>
#include <functional>
#include <paths.hpp>

#include "crypto/SHA256.hpp"
#include "drogon/HttpAppFramework.h"
#include "fgl/defines.hpp"
#include "logging/log.hpp"
#include "mime/MimeDatabase.hpp"

namespace idhan::modules
{

ModuleLoader::ModuleLoader() : m_libs(), m_modules()
{
	FGL_ASSERT( m_instance == nullptr, "ModuleLoader is a singleton" );
	m_instance = this;
	loadModules();
}

class ModuleHolder
{
#ifdef __linux__
	void* m_handle;
#elif defined( _WIN32 )
	HMODULE m_handle;
#endif

	using VoidFunc = void* (*)();
	VoidFunc initFunc { nullptr };
	VoidFunc deinitFunc { nullptr };

  public:

	FGL_DELETE_ALL_RO5( ModuleHolder );

#ifdef __linux__
	[[nodiscard]] void* handle() const { return m_handle; }
#elif defined( _WIN32 )
	[[nodiscard]] HMODULE handle() const { return m_handle; }
#endif

	ModuleHolder( const std::filesystem::path& path )
	{
		if ( !std::filesystem::exists( path ) )
		{
			log::critical( "Failed to find module at path {}", path.string() );
			std::abort();
		}

#ifdef __linux__
		m_handle = dlopen( path.c_str(), RTLD_LAZY | RTLD_GLOBAL );
		if ( !m_handle )
		{
			log::critical( "Failed to load module {}: {}", path.string(), dlerror() );
			std::abort();
		}

		initFunc   = reinterpret_cast< VoidFunc >( dlsym( m_handle, "init" ) );
		deinitFunc = reinterpret_cast< VoidFunc >( dlsym( m_handle, "deinit" ) );
#elif defined( _WIN32 )
		m_handle = LoadLibraryW( path.wstring().c_str() );
		if ( !m_handle )
		{
			log::critical( "Failed to load module {}: error {}", path.string(), GetLastError() );
			std::abort();
		}

		initFunc   = reinterpret_cast< VoidFunc >( GetProcAddress( m_handle, "init" ) );
		deinitFunc = reinterpret_cast< VoidFunc >( GetProcAddress( m_handle, "deinit" ) );
#endif

		if ( !initFunc )
		{
			log::critical( "Failed to find 'init' export in module {}", path.string() );
			std::abort();
		}

		if ( !deinitFunc )
		{
			log::critical( "Failed to find 'deinit' export in module {}", path.string() );
			std::abort();
		}

		initFunc();
	}

	~ModuleHolder()
	{
		deinitFunc();
#ifdef __linux__
		dlclose( m_handle );
#elif defined( _WIN32 )
		FreeLibrary( m_handle );
#endif
	}
};

namespace callbacks
{
std::expected< std::vector< std::byte >, ModuleError > generate(
	const data_view data,
	std::array< std::byte, 256 / 8 > hash,
	const Json::Value extra,
	const std::string file_name )
{
	auto mime_db { getMimeDatabase() };

	std::expected< std::string, drogon::HttpResponsePtr > exp {};

	drogon::async_run(
		[ & ]() -> drogon::Task< void >
		{
			auto coro = mime_db->scan( data, file_name );
			exp       = co_await coro;
		} );

	if ( !exp ) return std::unexpected( ModuleError { "Unable to scan for mime" } );
	{
		const auto generators { ModuleLoader::instance().getGeneratorsFor( *exp ) };

		if ( generators.empty() ) return std::unexpected( idhan::ModuleError { "No generator for given mime" } );

		const auto generator { generators.at( 0 ) };

		ModuleCallData call_data { .file_view = data, .extra = extra };

		const auto generated_file { generator->generate( call_data, hash ) };
		return generated_file;
	}
}

std::expected< ThumbnailInfo, ModuleError > thumbnail(
	const std::vector< std::byte >& data,
	const Json::Value extra,
	const std::string file_name )
{
	auto mime_db { getMimeDatabase() };

	std::expected< std::string, drogon::HttpResponsePtr > exp {};

	idhan::data_view data_view { reinterpret_cast< const unsigned char* >( data.data() ), data.size() };

	drogon::async_run(
		[ & ]() -> drogon::Task< void >
		{
			auto coro = mime_db->scan( data_view, file_name );
			exp       = co_await coro;
		} );

	if ( !exp ) return std::unexpected( ModuleError { "Unable to scan for mime" } );

	const auto thumbnailers { ModuleLoader::instance().getThumbnailerFor( *exp ) };
	if ( thumbnailers.empty() ) return std::unexpected( ModuleError { "Thumbnailers not found" } );

	const auto thumbnailer { thumbnailers.at( 0 ) };

	ModuleCallData call_data { .file_view = data_view, .mime_name = *exp, .extra = {} };

	return thumbnailer->createThumbnail( call_data, 128, 128 );
}

} // namespace callbacks

ModuleCallbacks generateCallbacks()
{
	return ModuleCallbacks { .thumbnail = &callbacks::thumbnail, .generate = &callbacks::generate };
}

void ModuleLoader::loadModules()
{
	const auto module_paths { getModulePaths() };

	for ( const std::filesystem::path& path : module_paths )
	{
		const auto extension { path.extension() };
		const auto name { path.filename().string() };

#ifdef __linux__
		constexpr std::string_view module_ext { ".so" };
#elif defined( _WIN32 )
		constexpr std::string_view module_ext { ".dll" };
#endif

		if ( extension != module_ext ) continue;

		log::info( "Library found: {}", name );

		std::shared_ptr< ModuleHolder > holder { std::make_shared< ModuleHolder >( path ) };
		m_libs.emplace_back( holder );

		if ( !holder->handle() )
		{
			log::error( "Failed to load module: {}", name );
			continue;
		}

		log::info( "Getting modules from shared lib" );

		using VoidFunc = void* (*)();

#ifdef __linux__
		const auto getModulesFunc { reinterpret_cast< VoidFunc >( dlsym( holder->handle(), "getModulesFunc" ) ) };
#elif defined( _WIN32 )
		const auto getModulesFunc {
			reinterpret_cast< VoidFunc >( GetProcAddress( holder->handle(), "getModulesFunc" ) )
		};
#endif

		if ( !getModulesFunc )
		{
			log::error( "Failed to find 'getModulesFunc' in module {}", name );
			continue;
		}

		using GetModulesFunc = std::vector< std::shared_ptr< IDHANModule > > ( * )( ModuleCallbacks );
		const auto getModules { reinterpret_cast< GetModulesFunc >( getModulesFunc() ) };

		if ( !getModules )
		{
			log::error( "getModulesFunc returned null in module {}", name );
			continue;
		}

		auto modules = getModules( generateCallbacks() );

		for ( const auto& module : modules )
		{
			log::info( "Interrogating module from {} named {}", name, module->name() );

			switch ( module->type() )
			{
				default:
					log::error( "Unknown module type: {}", module->type() );
					break;
				case ModuleTypeFlags::GENERATOR:
					log::info( "Module type: Generator" );
					break;
				case ModuleTypeFlags::METADATA:
					log::info( "Module type: Metadata" );
					break;
				case ModuleTypeFlags::THUMBNAILER:
					log::info( "Module type: Thumbnailer" );
					break;
			}

			m_modules.push_back( module );
		}
	}
}

std::vector< std::shared_ptr< ThumbnailerModuleI > > ModuleLoader::getThumbnailerFor( const std::string_view mime )
	const
{
	std::vector< std::shared_ptr< ThumbnailerModuleI > > ret {};

	if ( m_modules.empty() ) log::warn( "Tried to get thumbnailer for {} but no modules are loaded", mime );

	for ( const auto& module : m_modules )
	{
		if ( module->type() == ModuleTypeFlags::THUMBNAILER
		     && std::static_pointer_cast< ThumbnailerModuleI >( module )->canHandle( mime ) )
		{
			ret.push_back( std::static_pointer_cast< ThumbnailerModuleI >( module ) );
			return ret;
		}
	}
	return ret;
}

std::vector< std::shared_ptr< MetadataModuleI > > ModuleLoader::getParserFor( const std::string_view mime ) const
{
	std::vector< std::shared_ptr< MetadataModuleI > > ret {};

	if ( m_modules.empty() ) log::warn( "Tried to get parser for {} but no modules are loaded", mime );

	for ( const auto& module : m_modules )
	{
		if ( module->type() == ModuleTypeFlags::METADATA
		     && std::static_pointer_cast< MetadataModuleI >( module )->canHandle( mime ) )
		{
			ret.push_back( std::static_pointer_cast< MetadataModuleI >( module ) );
			return ret;
		}
	}
	return ret;
}

std::vector< std::shared_ptr< GeneratorModuleI > > ModuleLoader::getGeneratorsFor( std::string_view mime ) const
{
	std::vector< std::shared_ptr< GeneratorModuleI > > ret {};

	if ( m_modules.empty() ) log::warn( "Tried to get generator for {} but no modules are loaded", mime );

	for ( const auto& module : m_modules )
	{
		if ( module->type() == ModuleTypeFlags::GENERATOR
		     && std::static_pointer_cast< GeneratorModuleI >( module )->canHandle( mime ) )
		{
			ret.push_back( std::static_pointer_cast< GeneratorModuleI >( module ) );
			return ret;
		}
	}
	return ret;
}

} // namespace idhan::modules
