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

	static ModuleLoader& instance() { return *m_instance; }

	void loadModules();
	void unloadModules();

	std::vector< std::shared_ptr< ThumbnailerModuleI > > getThumbnailerFor( std::string_view mime ) const;
	std::vector< std::shared_ptr< MetadataModuleI > > getParserFor( std::string_view mime ) const;
	std::vector< std::shared_ptr< GeneratorModuleI > > getGeneratorsFor( std::string_view mime ) const;
};

} // namespace idhan::modules
