//
// Created by kj16609 on 6/9/22.
//

#ifndef MAIN_IDHANTHREADS_HPP
#define MAIN_IDHANTHREADS_HPP


/*
 * namespace Internal

 */


#include <tuple>
#include <atomic>
#include <future>
#include <thread>
#include <vector>
#include <queue>
#include <iostream>

#include <vips/vips8>
#include <vips/VImage8.h>

#include "utility/fileutils.hpp"
#include "utility/config.hpp"

#include <TracyBox.hpp>

namespace idhan::threads
{
	
	class ThumbnailerManager
	{
		inline static std::queue<std::tuple<std::vector<uint8_t>, std::vector<uint8_t>>> workQueue;
		inline static TracyLockable(std::mutex, workQueueLock)
		inline static std::vector<std::jthread> threads;
		
		static void run(std::stop_token stoken)
		{
			while(!stoken.stop_requested())
			{
				workQueueLock.lock();
				if ( workQueue.empty())
				{
					workQueueLock.unlock();
					std::this_thread::yield();
					continue;
				}
				
				auto [buffer, sha256] = std::move(workQueue.front());
				workQueue.pop();
				workQueueLock.unlock();
				
				vips::VImage img = vips::VImage::new_from_buffer(buffer.data(), buffer.size(), "");
				

			}
		}
		
	public:
		
		static void queue(const std::vector<uint8_t> buffer, const std::vector<uint8_t>& sha256)
		{
			workQueueLock.lock();
			workQueue.push( std::make_tuple( buffer, sha256 ));
			workQueueLock.unlock();
		}
		
		static void setThreadcount(size_t count)
		{
			workQueueLock.lock();
			if(threads.size() > count)
			{
				threads.resize(count);
			}
			else if(threads.size() < count)
			{
				for(size_t i = threads.size(); i < count; i++)
				{
					threads.emplace_back(&ThumbnailerManager::run);
				}
			}
			workQueueLock.unlock();
		}
		
		static void shutdown()
		{
			for(auto& thread : threads)
			{
				thread.request_stop();
				thread.join();
			}
		}
		
		static void wait()
		{
			while(true)
			{
				workQueueLock.lock();
				if(workQueue.empty())
				{
					workQueueLock.unlock();
					break;
				}
				workQueueLock.unlock();
				std::this_thread::yield();
				std::this_thread::sleep_for(std::chrono::milliseconds(250));
			}
			
			for(auto& thread : threads)
			{
				thread.request_stop();
				thread.join();
			}
		}
		
	};


}

#endif //MAIN_IDHANTHREADS_HPP
