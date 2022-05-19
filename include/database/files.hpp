//
// Created by kj16609 on 5/13/22.
//

#pragma once
#ifndef IDHAN_FILES_HPP
#define IDHAN_FILES_HPP

#include <pqxx/pqxx>

#include <vector>

namespace idhan::files
{
	uint64_t getHashID(std::vector<char> hash);

}



#endif //IDHAN_FILES_HPP
