//
// Created by kj16609 on 6/9/22.
//

#ifndef MAIN_FILEUTILS_HPP
#define MAIN_FILEUTILS_HPP

#include <string>
#include <vector>
#include <iomanip>

#include "MrMime/mister_mime.hpp"

namespace idhan::utils
{
	template <typename T>
	std::string toHex(T data)
	{
		std::stringstream ss;
		for (auto& i : data)
		{
			ss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(i);
		}
		
		return ss.str();
	}
	
	std::pair<MrMime::FileType, uint64_t> get_mime(const std::string& filepath);
}

#endif //MAIN_FILEUTILS_HPP
