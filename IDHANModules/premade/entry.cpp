//
// Created by kj16609 on 6/11/25.
//

#include <vips/vips.h>

#include <memory>
#include <vector>

#include "ImageVipsMetadata.hpp"
#include "ImageVipsThumbnailer.hpp"

ModuleInfo IDHANEntry()
{}

std::vector< std::shared_ptr< IDHANModule > > getModules()
{
	std::vector< std::shared_ptr< IDHANModule > > ret { std::make_shared< ImageVipsMetadata >(),
		                                                std::make_shared< ImageVipsThumbnailer >() };

	return ret;
}

extern "C" {

void* getEntryFunc()
{
	return reinterpret_cast< void* >( &IDHANEntry );
}

void* getModulesFunc()
{
	return reinterpret_cast< void* >( &getModules );
}

// Defined and implemented if a library used by these modules must be initalized first
void init()
{
	VIPS_INIT( "IDHANPremadeModules" );
}

// Defined and implemented if a library used by these modules must be initalized first
void deinit()
{
	vips_shutdown();
}
}
