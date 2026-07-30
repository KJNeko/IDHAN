//
// Created by kj16609 on 6/11/25.
//

#include <memory>
#include <vector>

#include "ImageVipsMetadata.hpp"
#include "ImageVipsThumbnailer.hpp"
#include "vipsInit.hpp"

using namespace idhan;

//! The module instances this library exports.
/** Order is the library's ABI: the host addresses a module by its index in this vector, and a
 *  worker validates the order against the manifest it was registered with. Append only -- never
 *  reorder or remove an entry without accepting that running servers must re-interrogate. */
std::vector< std::shared_ptr< IDHANModule > > getModules( ModuleCallbacks callbacks )
{
	return { std::make_shared< ImageVipsMetadata >( callbacks ),
		     std::make_shared< ImageVipsThumbnailer >( callbacks ) };
}

extern "C" {

void* getModulesFunc()
{
	return reinterpret_cast< void* >( &getModules );
}

void init()
{
	premade::vipsInit( "IDHANVips" );
}

void deinit()
{
	premade::vipsShutdown();
}
}
