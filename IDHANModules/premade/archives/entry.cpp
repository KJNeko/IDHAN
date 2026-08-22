#include <memory>
#include <vector>

#include "ArchiveGenerator.hpp"
#include "ArchiveMetadata.hpp"
#include "ArchiveMimeParser.hpp"
#include "ArchiveThumbnailer.hpp"
#include "vipsInit.hpp"

using namespace idhan;

//! The module instances this library exports.
std::vector< std::shared_ptr< IDHANModule > > getModules( ModuleCallbacks callbacks )
{
	return { std::make_shared< ArchiveMetadata >( callbacks ),
		     std::make_shared< ArchiveGenerator >( callbacks ),
		     std::make_shared< ArchiveThumbnailer >( callbacks ),
		     std::make_shared< ArchiveMimeParser >( callbacks ) };
}

extern "C" {

void* getModulesFunc()
{
	return reinterpret_cast< void* >( &getModules );
}

void init()
{
	// The thumbnailer composites member thumbnails into a grid with vips_black/vips_insert.
	premade::vipsInit( "IDHANArchive" );
}

void deinit()
{
	premade::vipsShutdown();
}
}
