//
// Created by kj16609 on 7/28/26.
//

#include <memory>
#include <vector>

#include "PsdMetadata.hpp"
#include "PsdThumbnailer.hpp"
#include "vipsInit.hpp"

using namespace idhan;

//! The module instances this library exports.
/** Order is the library's ABI: the host addresses a module by its index in this vector, and a
 *  worker validates the order against the manifest it was registered with. Append only -- never
 *  reorder or remove an entry without accepting that running servers must re-interrogate. */
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
