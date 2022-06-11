//
// Created by kj16609 on 6/9/22.
//

#include "fileutils.hpp"

#include <vector>
#include <string>
#include <iomanip>

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
}

