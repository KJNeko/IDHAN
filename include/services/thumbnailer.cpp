//
// Created by kj16609 on 6/11/22.
//

#include "database.hpp"
#include "thumbnailer.hpp"
#include "utility/config.hpp"
#include "utility/fileutils.hpp"
#include <future>

#include "TracyBox.hpp"
namespace idhan::services
{
	void ImageThumbnailer::start(size_t count)
	{
		std::lock_guard<std::mutex> lock(queuelock);
		for (size_t i = threads.size(); i < count; i++)
		{
			threads.emplace_back(&ImageThumbnailer::run);
		}
	}
	
	void ImageThumbnailer::run(std::stop_token token)
	{
		//Make a new connection to the database
		
		while(!token.stop_requested())
		{
			queuelock.lock();
			if(queue.size() > 0)
			{
				//Manage the thumbnailer threads

				ZoneScopedN("Thumbnailer");
				TracyCPlot("Thumbnail queue size", static_cast<double>(queue.size()));
				auto [promise, hashID] = std::move(queue.front());
				queue.pop();
				queuelock.unlock();
				
				//Get sha from hashID
				std::string sha256_view;
				std::string extention;
				{
					ZoneScopedN("getSHA256");
					Connection conn;
					pqxx::work wrk(conn.getConn());
					pqxx::result res = wrk.exec_prepared("selectFileSHA256", hashID);
					
					if(res.size() == 0)
					{
						promise.set_value(vips::VImage());
						wrk.commit();
						continue;
					}
					
					sha256_view = res[0][0].as<std::string>();
					wrk.commit();
					
					pqxx::work wrk2(conn.getConn());
					
					pqxx::result res2 = wrk2.exec_prepared("selectFilename", hashID);
					if(res2.size() == 0)
					{
						promise.set_value(vips::VImage());
						wrk.commit();
						continue;
					}
					
					std::string filename = res2[0][0].as<std::string>();
					//Extract the extention
					extention = filename.substr(filename.find_last_of('.') + 1);
					
					wrk.commit();
				}
				
				std::string fileHex = sha256_view.substr(2, sha256_view.size() - 2).data();
				
				auto path = idhan::config::fileconfig::file_path;
				path /= "f" + fileHex.substr(0,2);
				path /= fileHex + "." + extention;
				
				auto thumbnailPath = idhan::config::fileconfig::thumbnail_path;
				thumbnailPath /= "t" + fileHex.substr(0,2);
				thumbnailPath /= fileHex + ".png";
				
				if(std::filesystem::exists(path) && !std::filesystem::exists(thumbnailPath))
				{
					TracyCZoneN(Tracythumbnail, "Thumbnail", true);
					//Load image
					TracyCZoneN(read, "readFile", true);
					vips::VImage image { vips::VImage::new_from_file(path.c_str(), vips::VImage::option()->set("access", VIPS_ACCESS_SEQUENTIAL)) };
					TracyCZoneEnd(read);
					
					//Calculate resize ratio
					TracyCZoneN(resize, "Resize Image", true);
					const double ratio = std::min(
						static_cast<double>(idhan::config::fileconfig::thumbnail_width) / static_cast<double>(image.width()),
						static_cast<double>(idhan::config::fileconfig::thumbnail_height) / static_cast<double>(image.height()));
					
					uint64_t width = static_cast<uint64_t>(
							static_cast<double>(image.width()) * ratio);
					//vips::VImage resized = image.resize(ratio);
					vips::VImage resized = image.thumbnail_image(static_cast<int>(width));
					TracyCZoneEnd(resize);
					
					//Save image to disk
					if(!std::filesystem::exists(thumbnailPath.parent_path()))
						std::filesystem::create_directories(thumbnailPath.parent_path());
					TracyCZoneN(writeFile, "writeFile", true);
					resized.write_to_file(thumbnailPath.c_str());
					TracyCZoneEnd(writeFile);
					TracyCZoneEnd(Tracythumbnail);
				}
				else
				{
					continue;
				}
			}
			else
			{
				queuelock.unlock();
				std::this_thread::yield();
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			}
			
		}
	}
	
	
	void ImageThumbnailer::enqueue(const uint64_t hashID, std::promise<vips::VImage> promise)
	{
		static std::chrono::steady_clock::time_point lastChecked {std::chrono::milliseconds(0)};
		
		std::lock_guard<std::mutex> lock(queuelock);
		queue.emplace( std::move( promise ), hashID );
		
		TracyCPlot("Thumbnail queue size", static_cast<double>(queue.size()));
	}
	
	void ImageThumbnailer::await()
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
	
	void ImageThumbnailer::manageThreads()
	{
		static std::chrono::steady_clock::time_point lastChecked = std::chrono::steady_clock::now();
		static std::mutex checkLock;
		
		if(lastChecked < std::chrono::steady_clock::now() - std::chrono::seconds(2))
		{
			if ( queue.size() > threads.size() * 25 )
			{
				TracyCPlot( "Thumbnail threads", static_cast<double>(threads.size()));
				if ( threads.size() <
					 idhan::config::services::service_maximum_threads )
					threads.emplace_back( &ImageThumbnailer::run );
			}
			else
			{
				TracyCPlot( "Thumbnail threads", static_cast<double>(threads.size()));
				//If the queue is under 5 remove a thread until there is only one left
				if ( queue.size() < threads.size() * 15 )
				{
					if ( threads.size() > 1 )
					{
						threads.back().request_stop();
						threads.back().join();
						threads.pop_back();
					}
				}
			}
			lastChecked = std::chrono::steady_clock::now();
		}
	}
	
	ImageThumbnailer::~ImageThumbnailer()
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
	
	void Thumbnailer::work( std::stop_token stoken )
	{
		while(!stoken.stop_requested())
		{
			ImageThumbnailer::manageThreads();
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
				//case MrMime::IMAGE_APNG: [[fallthrough]];
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
				
				}
					break;
				
				case MrMime::APPLICATION_FLASH:
					break;
				case MrMime::APPLICATION_PSD:
					//TODO: Figure out. Maybe ask floo
					break;
					
					//Audio
				case MrMime::AUDIO_WAVE:
					break;
				case MrMime::AUDIO_FLAC:
					break;
				
				default:
					throw std::runtime_error("Unknown mime type or file is not supported");
			}
		}
	}
	
	void Thumbnailer::start()
	{
		thread = std::jthread(&Thumbnailer::work);
	}
	
	std::future<vips::VImage> Thumbnailer::enqueue( const uint64_t hashID )
	{
		std::lock_guard<std::mutex> lock( queuelock );
		std::promise<vips::VImage> promise;
		queue.emplace( hashID, std::move( promise ));
		return queue.back().second.get_future();
	}
	
	void Thumbnailer::await()
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
	
	Thumbnailer::~Thumbnailer()
	{
		thread.request_stop();
		thread.join();
	}
	
}


