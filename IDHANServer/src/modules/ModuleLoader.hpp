//
// Created by kj16609 on 6/11/25.
//
#pragma once
#include <memory>
#include <vector>

#include "GeneratorModule.hpp"
#include "MetadataModule.hpp"
#include "ModuleBase.hpp"
#include "ThumbnailerModule.hpp"

namespace idhan::modules
{
class ModuleHolder;

//! Loads the premade/plugin modules at startup (dlopen of every shared object in the modules
//! directory) and resolves, by MIME type, which module handles a thumbnail, metadata or generate
//! request. Owns the loaded libraries and module instances; singleton via instance().
class ModuleLoader
{
	// m_libs must be declared before m_modules: the module objects (code, vtables and
	// shared_ptr control blocks) live inside the dlopened libraries, so the modules must be
	// destroyed before the ModuleHolders dlclose them
	std::vector< std::shared_ptr< ModuleHolder > > m_libs;
	std::vector< std::shared_ptr< IDHANModule > > m_modules;
	inline static ModuleLoader* m_instance;

  public:

	ModuleLoader();

	//! \return The process-wide ModuleLoader singleton.
	static ModuleLoader& instance() { return *m_instance; }

	//! dlopens every module in the modules directory and registers the modules each one exports.
	void loadModules();
	//! Releases the module instances and closes the loaded shared libraries.
	void unloadModules();

	//! \return The thumbnailer modules that can handle \p mime (currently at most one).
	std::vector< std::shared_ptr< ThumbnailerModuleI > > getThumbnailerFor( std::string_view mime ) const;
	//! \return The metadata parser modules that can handle \p mime (currently at most one).
	std::vector< std::shared_ptr< MetadataModuleI > > getParserFor( std::string_view mime ) const;
	//! \return The generator modules that can handle \p mime (currently at most one).
	std::vector< std::shared_ptr< GeneratorModuleI > > getGeneratorsFor( std::string_view mime ) const;
};

} // namespace idhan::modules
