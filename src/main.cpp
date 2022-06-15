//
// Created by kj16609 on 6/1/22.
//

#include <iostream>

#include <vector>

#include "database.hpp"
#include "crypto.hpp"
#include "./services/thumbnailer.hpp"

#include <vips/vips8>
#include <vips/VImage8.h>

#include <TracyBox.hpp>

void resetDB()
{
	ConnectionRevolver::resetDB();
}

int main(int argc, char** argv)
{
	idhan::services::ImageThumbnailer::start();
	idhan::services::Thumbnailer::start();
	
	//VIPS
	if(VIPS_INIT(argv[0]))
	{
		throw std::runtime_error("Failed to initialize vips");
	}
	
	//Catch2
	
	ZoneScopedN("MassAdd");
	resetDB();
	std::filesystem::path path {"../Images/Perf/JPG"};
	
	std::vector<std::string> files;
	
	for(auto& p : std::filesystem::directory_iterator(path))
	{
		files.push_back(p.path().string());
	}
	
	//Add files
	std::vector<uint64_t> ids;
	for(auto& f : files)
	{
		ids.push_back(addFile(f));
	}
	
	
	TracyCZoneN(await,"Shutdown",true);
	idhan::services::ImageThumbnailer::await();
	idhan::services::Thumbnailer::await();
	TracyCZoneEnd(await);
	
	//VIPS
	vips_shutdown();
	
	return 0;
}