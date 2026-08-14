#include <memory>
#include <vector>

#include "PsdMetadata.hpp"
#include "PsdThumbnailer.hpp"
#include "vipsInit.hpp"

using namespace idhan;

//! The module instances this library exports.
std::vector< std::shared_ptr< IDHANModule > > getModules( ModuleCallbacks callbacks )
{
	return { std::make_shared< PsdMetadata >( callbacks ), std::make_shared< PsdThumbnailer >( callbacks ) };
}

extern "C" {

void* getModulesFunc()
{
	return reinterpret_cast< void* >( &getModules );
}

void init()
{
	premade::vipsInit( "IDHANPsd" );
}

void deinit()
{
	premade::vipsShutdown();
}
}
