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
		
		inline static void run(std::stop_token);
	public:
		
		static void start(size_t count = 4);
		
		//Priority
		static void enqueue(const uint64_t hashID, std::promise<vips::VImage> promise);
		
		static void await()
		{
			while(true)
			{
				std::this_thread::yield();
				
				queuelock.lock();
				if(queue.empty())
				{
					queuelock.unlock();
					return;
				}
				queuelock.unlock();
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			}
		}
		
		~ImageThumbnailer()
		{
			for(auto& thread : threads)
			{
				thread.request_stop();
			}
			
			for(auto& thread : threads)
			{
				thread.join();
			}
		}
	
	};
	
	class Thumbnailer
	{
	public:
		
		inline static std::queue<std::pair<uint64_t, std::promise<vips::VImage>>> queue;
		inline static std::mutex queuelock;
		
		inline static std::jthread thread;
		

		
		inline static void work(std::stop_token stoken)
		{
			while(!stoken.stop_requested())
			{
				queuelock.lock();
				if ( queue.empty() )
				{
					queuelock.unlock();
					std::this_thread::yield();
					std::this_thread::sleep_for(std::chrono::milliseconds(100));
					continue;
				}
				auto [hashID, promise] = std::move(queue.front());
				queue.pop();
				queuelock.unlock();
				
				ZoneScopedN("ThumbnailerEnqueue");
				//Get mime type from DB
				MrMime::FileType mimeNum;
				
				{
					ZoneScopedN("getMime");
					Connection conn;
					pqxx::work wrk(conn.getConn());
					pqxx::result res = wrk.exec_prepared("selectFileMimeType", hashID);
					if(res.size() == 0)
					{
						wrk.commit();
						promise.set_exception(std::make_exception_ptr(std::runtime_error("Mapping for file not found")));
						continue;
					}
					
					mimeNum = static_cast<MrMime::FileType>(res[0][0].as<uint16_t>());
				}
				
				switch(mimeNum)
				{
					//Images
					case MrMime::IMAGE_JPEG: [[fallthrough]];
					case MrMime::IMAGE_PNG: [[fallthrough]];
					case MrMime::IMAGE_GIF: [[fallthrough]];
					case MrMime::IMAGE_BMP: [[fallthrough]];
					case MrMime::IMAGE_WEBP: [[fallthrough]];
					case MrMime::IMAGE_TIFF: [[fallthrough]];
					case MrMime::IMAGE_APNG: [[fallthrough]];
					case MrMime::IMAGE_ICON:
					{
						ImageThumbnailer::enqueue(hashID, std::move(promise));
					}
						break;
						
						//Video
					case MrMime::VIDEO_FLV: [[fallthrough]];
					case MrMime::VIDEO_MP4: [[fallthrough]];
					case MrMime::VIDEO_MOV: [[fallthrough]];
					case MrMime::VIDEO_AVI:
					{
						//FFMPEG
					}
						break;
					
					case MrMime::APPLICATION_FLASH:
						break;
					case MrMime::APPLICATION_PSD:
						//TODO: Figure out. Maybe ask floo
						break;
					case MrMime::APPLICATION_CLIP:
						break;
						
						//Audio
					case MrMime::AUDIO_WAVE:
						break;
					case MrMime::AUDIO_FLAC:
						break;
						
						//Unknown
					case MrMime::UNDETERMINED_WM:
						break;
					
					default:
						break;
				}
			}
		}
		
		static inline void start()
		{
			thread = std::jthread(&Thumbnailer::work);
		}
		
		inline static std::future<vips::VImage> enqueue( const uint64_t hashID )
		{
			std::lock_guard<std::mutex> lock( queuelock );
			std::promise<vips::VImage> promise;
			queue.emplace( hashID, std::move( promise ));
			return queue.back().second.get_future();
		}
		
		static void await()
		{
			while(true)
			{
				std::this_thread::yield();
				
				queuelock.lock();
				if(queue.empty())
				{
					queuelock.unlock();
					return;
				}
				queuelock.unlock();
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			}
		}
		
		~Thumbnailer()
		{
			thread.request_stop();
			thread.join();
		}
	};
}




#endif //MAIN_THUMBNAILER_HPP
