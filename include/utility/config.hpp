//
// Created by kj16609 on 6/9/22.
//

#ifndef MAIN_CONFIG_HPP
#define MAIN_CONFIG_HPP

#include <atomic>
#include <filesystem>

namespace idhan
{
	struct config
	{
		inline static std::atomic<uint64_t> thumbnail_width { 250 };
		inline static std::atomic<uint64_t> thumbnail_height { 250 };
		
		inline static std::mutex configLock;
		
		inline static std::filesystem::path thumbnail_path { "./db/thumbnails/" };
		inline static std::atomic<bool> thumbnail_pathValid = false;
		
		inline static std::filesystem::path file_path { "./db/file/"};
		
		
		//vips
		inline static std::atomic<bool> vips_allow_threaded;
		
		
	};
}


#endif //MAIN_CONFIG_HPP
