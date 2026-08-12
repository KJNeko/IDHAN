#include <memory>
#include <vector>

#include "FFMPEGMetadata.hpp"
#include "FFMPEGThumbnailer.hpp"
#include "vipsInit.hpp"

using namespace idhan;

//! The module instances this library exports.
/** Order is the library's ABI: the host addresses a module by its index in this vector, and a
 *  worker validates the order against the manifest it was registered with. Append only -- never
 *  reorder or remove an entry without accepting that running servers must re-interrogate. */
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
	// FFmpeg needs no explicit registration on the versions we target, but the thumbnailer encodes
	// its result through ThumbnailerModuleI::createThumbnailFile, which is vips.
	premade::vipsInit( "IDHANFFmpeg" );
}

void deinit()
{
	premade::vipsShutdown();
}
}
