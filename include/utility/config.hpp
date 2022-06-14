//
// Created by kj16609 on 6/9/22.
//

#ifndef MAIN_CONFIG_HPP
#define MAIN_CONFIG_HPP

#include <atomic>
#include <filesystem>

namespace idhan::config
{
	inline static std::mutex configLock;
	
	//Import and file configs
	struct fileconfig
	{
		//File storage
		inline static std::filesystem::path file_path { "./db/file/"};
		
		
		//Thumbnail stuff
		inline static std::atomic<uint64_t> thumbnail_width { 250 };
		inline static std::atomic<uint64_t> thumbnail_height { 250 };
		inline static std::filesystem::path thumbnail_path { "./db/thumbnails/" };
		inline static std::atomic<bool> thumbnail_pathValid { false };
	};

	
	//vips
	struct vipsconfig
	{
		inline static std::atomic<bool> vips_allow_threaded;
	};
	
	
	
	inline static std::atomic<bool> debug { true };
	inline static std::atomic<bool> thumbnail_active { false };
	
	struct services
	{
		inline static std::atomic<size_t> service_maximum_threads { 0 };
		
		//Grace period in ms for services to start
		inline static std::atomic<size_t> service_grace_period { 1000 };
	};
}


#endif //MAIN_CONFIG_HPP
