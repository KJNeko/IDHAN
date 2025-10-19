//
// Created by kj16609 on 6/11/25.
//
#include "ModuleLoader.hpp"

#include <dlfcn.h>
#include <filesystem>
#include <functional>
#include <paths.hpp>

#include "fgl/defines.hpp"
#include "logging/log.hpp"

namespace idhan::modules
{

ModuleLoader::ModuleLoader() : m_modules(), m_libs()
{
	FGL_ASSERT( m_instance == nullptr, "ModuleLoader is a singleton" );
	m_instance = this;
	loadModules();
}

class ModuleHolder
{
	void* m_handle;
	using VoidFunc = void* (*)();
	VoidFunc initFunc { nullptr };
	VoidFunc deinitFunc { nullptr };

  public:

	FGL_DELETE_ALL_RO5( ModuleHolder );

	[[nodiscard]] void* handle() const { return m_handle; }

	ModuleHolder( const std::filesystem::path& path ) : m_handle( dlopen( path.c_str(), RTLD_LAZY | RTLD_GLOBAL ) )
	{
		if ( !std::filesystem::exists( path ) )
		{
			log::critical( "Failed to find module at path {}, {}", path.string(), dlerror() );
			std::abort();
		}

		if ( !m_handle )
		{
			log::critical( "Failed to load module {}, {}", path.string(), dlerror() );
			std::abort();
		}

		initFunc = reinterpret_cast< VoidFunc >( dlsym( m_handle, "init" ) );
		if ( !initFunc )
		{
			log::critical( "Failed to interrogate module {}, deinitFunc not found", path.string() );
			std::abort();
		}

		deinitFunc = reinterpret_cast< VoidFunc >( dlsym( m_handle, "deinit" ) );
		if ( !deinitFunc )
		{
			log::critical( "Failed to interrogate module {}, initFunc not found", path.string() );
			std::abort();
		}

		initFunc();
	}

	~ModuleHolder()
	{
		deinitFunc();
		dlclose( m_handle );
	}
};

void ModuleLoader::loadModules()
{
	const auto module_paths { getModulePaths() };

	for ( const std::filesystem::path& path : module_paths )
	{
		const auto extension { path.extension() };
		const auto name { path.filename().string() };

		if ( extension == ".so" )
		{
			log::info( "Library found: {}", name );

			// void* handle = dlopen( entry.path().c_str(), RTLD_LAZY | RTLD_GLOBAL );
			std::shared_ptr< ModuleHolder > holder { std::make_shared< ModuleHolder >( path ) };
			m_libs.emplace_back( holder );

			if ( !holder->handle() )
			{
				log::error( "Failed to load module: {}", dlerror() );
				continue;
			}

			log::info( "Getting modules from shared lib" );

			using VoidFunc = void* (*)();
			auto getModulesFunc { reinterpret_cast< VoidFunc >( dlsym( holder->handle(), "getModulesFunc" ) ) };
			if ( !getModulesFunc )
			{
				log::error( "Failed to get getModulesFunc: {}", dlerror() );
				continue;
			}

			using GetModulesFunc = std::vector< std::shared_ptr< IDHANModule > > ( * )();
			auto getModules { reinterpret_cast< GetModulesFunc >( getModulesFunc() ) };

			if ( !getModules )
			{
				log::error( "Failed to get modules function: {}", dlerror() );
				continue;
			}

			auto modules = getModules();

			//TODO: Possibly UB, Since apparently libraries have their own heaps?

			for ( const auto& module : modules )
			{
				log::info( "Interrogating module from {} named {}", name, module->name() );

				switch ( module->type() )
				{
					default:
						log::error( "Unknown module type: {}", module->type() );
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
}

std::vector< std::shared_ptr< ThumbnailerModuleI > > ModuleLoader::getThumbnailerFor( const std::string_view mime )
{
	std::vector< std::shared_ptr< ThumbnailerModuleI > > ret {};
	for ( const auto& module : m_modules )
	{
		if ( module->type() == ModuleTypeFlags::THUMBNAILER
		     && std::static_pointer_cast< ThumbnailerModuleI >( module )->canHandle( mime ) )
		{
			ret.push_back( std::static_pointer_cast< ThumbnailerModuleI >( module ) );
		}
	}
	return ret;
}

std::vector< std::shared_ptr< MetadataModuleI > > ModuleLoader::getParserFor( const std::string_view mime )
{
	std::vector< std::shared_ptr< MetadataModuleI > > ret {};
	for ( const auto& module : m_modules )
	{
		if ( module->type() == ModuleTypeFlags::METADATA
		     && std::static_pointer_cast< MetadataModuleI >( module )->canHandle( mime ) )
		{
			ret.push_back( std::static_pointer_cast< MetadataModuleI >( module ) );
		}
	}
	return ret;
}

} // namespace idhan::modules