//
// Created by kj16609 on 6/11/25.
//
#include "ModuleLoader.hpp"

#include <dlfcn.h>
#include <filesystem>
#include <functional>

#include "fgl/defines.hpp"
#include "logging/log.hpp"

namespace idhan::modules
{

ModuleLoader::ModuleLoader()
{
	FGL_ASSERT( m_instance == nullptr, "ModuleLoader is a singleton" );
	m_instance = this;
	loadModules();
}

class ModuleHolder
{
	void* m_handle;
	using VoidFunc = void* (*)();
	VoidFunc initFunc;
	VoidFunc deinitFunc;

  public:

	void* handle() const { return m_handle; }

	ModuleHolder( const std::filesystem::path path ) :
	  m_handle( dlopen( path.c_str(), RTLD_LAZY | RTLD_GLOBAL ) ),
	  initFunc( reinterpret_cast< VoidFunc >( dlsym( m_handle, "init" ) ) ),
	  deinitFunc( reinterpret_cast< VoidFunc >( dlsym( m_handle, "deinit" ) ) )
	{
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
	const auto search_path { std::filesystem::current_path() / "modules" };

	log::info( "Searching for modules at {}", search_path.string() );

	for ( const auto& entry : std::filesystem::directory_iterator( search_path ) )
	{
		if ( !entry.is_regular_file() ) continue;

		const auto path { entry.path() };
		const auto extension { path.extension() };
		const auto name { path.filename().string() };

		if ( extension == ".so" )
		{
			log::info( "Module found: {}", name );

			// void* handle = dlopen( entry.path().c_str(), RTLD_LAZY | RTLD_GLOBAL );
			std::shared_ptr< ModuleHolder > holder { std::make_shared< ModuleHolder >( entry ) };
			m_libs.emplace_back( holder );

			if ( !holder->handle() )
			{
				log::error( "Failed to load module: {}", dlerror() );
				continue;
			}

			log::info( "Getting modules from module" );

			using EntryFunc = void* (*)();
			auto getModulesFunc { reinterpret_cast< EntryFunc >( dlsym( holder->handle(), "getModulesFunc" ) ) };
			if ( !getModulesFunc )
			{
				log::error( "Failed to get getModulesFunc: {}", dlerror() );
				continue;
			}

			using GetModulesFunc = std::vector< std::shared_ptr< IDHANModule > > ( * )();
			GetModulesFunc getModules { reinterpret_cast< GetModulesFunc >( getModulesFunc() ) };

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