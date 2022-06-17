//
// Created by kj16609 on 6/9/22.
//

#ifndef MAIN_FILEUTILS_HPP
#define MAIN_FILEUTILS_HPP

#include <string>
#include <vector>

#include "MrMime/mister_mime.hpp"

namespace idhan::utils
{
	std::string toHex(std::vector<uint8_t> data);
	
	std::pair<MrMime::FileType, uint64_t> get_mime(const std::string& filepath);
}

#endif //MAIN_FILEUTILS_HPP
