#include <malloc.h>
#include <memory>
#include <vector>

#include "ImageVipsMetadata.hpp"
#include "ImageVipsThumbnailer.hpp"
#include "vipsInit.hpp"

using namespace idhan;

//! The module instances this library exports.
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
