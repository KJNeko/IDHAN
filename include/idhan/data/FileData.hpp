//
// Created by kj16609 on 8/26/22.
//

#pragma once
#ifndef IDHAN_FILEDATA_HPP
#define IDHAN_FILEDATA_HPP




#include <array>


class FileRecord
{
	uint64_t file_id;

	std::array<std::byte, 32> sha256;




};




#endif	// IDHAN_FILEDATA_HPP
