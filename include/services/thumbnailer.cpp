//
// Created by kj16609 on 6/11/22.
//

#include "database.hpp"
#include "thumbnailer.hpp"
#include "utility/config.hpp"
#include "utility/fileutils.hpp"
#include <future>

#include "TracyBox.hpp"

#include <iostream>

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
				ZoneScopedN("Thumbnailer");
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
						continue;
					}
					
					sha256_view = res[0][0].as<std::string>();
					
					pqxx::result res2 = wrk.exec_prepared("selectFilename", hashID);
					if(res2.size() == 0)
					{
						promise.set_value(vips::VImage());
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
					
					uint64_t width = image.width() * ratio;
					//vips::VImage resized = image.resize(ratio);
					vips::VImage resized = image.thumbnail_image(width);
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
		std::lock_guard<std::mutex> lock(queuelock);
		queue.emplace(std::move(promise), hashID);
	}
	
}


