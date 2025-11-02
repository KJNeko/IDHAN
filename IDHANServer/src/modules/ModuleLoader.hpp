//
// Created by kj16609 on 6/11/25.
//
#pragma once
#include <memory>
#include <vector>

#include "MetadataModule.hpp"
#include "ModuleBase.hpp"
#include "ThumbnailerModule.hpp"

namespace idhan::modules
{
class ModuleHolder;

class ModuleLoader
{
	std::vector< std::shared_ptr< IDHANModule > > m_modules;
	std::vector< std::shared_ptr< ModuleHolder > > m_libs;
	inline static ModuleLoader* m_instance;

  public:

	ModuleLoader();

	static ModuleLoader& instance() { return *m_instance; }

	void loadModules();
	void unloadModules();

	std::vector< std::shared_ptr< ThumbnailerModuleI > > getThumbnailerFor( std::string_view mime ) const;
	std::vector< std::shared_ptr< MetadataModuleI > > getParserFor( std::string_view mime ) const;
};

} // namespace idhan::modules
