#include <memory>
#include <vector>

#include "FFMPEGMetadata.hpp"
#include "FFMPEGThumbnailer.hpp"
#include "vipsInit.hpp"

using namespace idhan;

//! The module instances this library exports.
std::vector< std::shared_ptr< IDHANModule > > getModules( ModuleCallbacks callbacks )
{
	return { std::make_shared< FFMPEGMetadata >( callbacks ), std::make_shared< FFMPEGThumbnailer >( callbacks ) };
}

extern "C" {

void* getModulesFunc()
{
	return reinterpret_cast< void* >( &getModules );
}

void init()
{
	premade::vipsInit( "IDHANFFmpeg" );
}

void deinit()
{
	premade::vipsShutdown();
}
}
