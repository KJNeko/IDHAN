//
// Created by kj16609 on 6/11/22.
//

#ifndef MAIN_THUMBNAILER_HPP
#define MAIN_THUMBNAILER_HPP

#include <vips/vips8>

#include <future>
#include <queue>
#include <thread>
#include <coroutine>

#include "MrMime/mister_mime.hpp"

namespace idhan::services
{
	class ImageThumbnailer
	{
		inline static std::vector<std::jthread> threads;
		
		inline static std::queue<std::pair<std::promise<vips::VImage>, uint64_t>> queue;
		inline static std::mutex queuelock;
		
		static void run(std::stop_token);
	public:
		
		static void start(size_t count = 1);
		
		//Priority
		static void enqueue(const uint64_t hashID, std::promise<vips::VImage> promise);
		
		static void await();
		
		static void manageThreads();
		
		~ImageThumbnailer();
	
	};
	
	class Thumbnailer
	{
	public:
		
		inline static std::queue<std::pair<uint64_t, std::promise<vips::VImage>>> queue;
		inline static std::mutex queuelock;
		
		inline static std::jthread thread;
		
		static void work(std::stop_token stoken);
		
		static void start();
		
		static std::future<vips::VImage> enqueue( const uint64_t hashID );
		
		static void await();
		

		
		~Thumbnailer();
	};
}




#endif //MAIN_THUMBNAILER_HPP
