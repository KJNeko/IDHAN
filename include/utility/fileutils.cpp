//
// Created by kj16609 on 6/9/22.
//

#include "fileutils.hpp"

#include <vector>
#include <string>
#include <iomanip>

#include <filesystem>
#include <fstream>

#include "MrMime/mister_mime.hpp"

namespace idhan::utils
{
	std::string toHex(std::vector<uint8_t> data)
	{
		std::stringstream ss;
		for (auto& i : data)
		{
			ss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(i);
		}
		
		return ss.str();
	}
	
	std::pair<MrMime::FileType, uint64_t> get_mime(const std::string& filepath)
	{
		using namespace MrMime;
		
		//Verify that the filepath is correct
		if(filepath.empty() || !std::filesystem::exists(filepath))
		{
			return {FileType::APPLICATION_UNKNOWN, 0};
		}
		
		//Get the mime type
		using MrMime::header_data_buffer_t;
		
		const std::size_t size {std::filesystem::file_size(filepath)};
		
		
		if ( size < sizeof( header_data_buffer_t ))
		{
			return {FileType::APPLICATION_UNKNOWN, size};
		}
		
		//Read the header data
		if(std::ifstream file(filepath, std::ios::binary); file)
		{
			header_data_buffer_t header_data;
			file.read(reinterpret_cast<char*>(&header_data), sizeof(header_data));
			
			//Get the mime type
			const auto MIMEType {MrMime::deduceFileType(header_data)};
			
			return {MIMEType, size};
		}
		else
		{
			return {FileType::APPLICATION_UNKNOWN, size};
		}
	}
	
}

