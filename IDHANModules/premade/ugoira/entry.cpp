#include <memory>
#include <vector>

#include "UgoiraThumbnailer.hpp"
#include "vipsInit.hpp"

using namespace idhan;

//! The module instances this library exports.
std::vector< std::shared_ptr< IDHANModule > > getModules( ModuleCallbacks callbacks )
{
	return { std::make_shared< UgoiraThumbnailer >( callbacks ) };
}

extern "C" {

void* getModulesFunc()
{
	return reinterpret_cast< void* >( &getModules );
}

void init()
{
	premade::vipsInit( "IDHANUgoira" );
}

void deinit()
{
	premade::vipsShutdown();
}
}
