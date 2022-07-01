//
// Created by kj16609 on 7/1/22.
//

#ifndef MAIN_FILEDATA_HPP
#define MAIN_FILEDATA_HPP

#include <filesystem>

#include "files.hpp"

#include <QByteArray>

struct FileData
{
	std::filesystem::path path;

	QByteArray file_bytes;
	Hash32 sha256;
	uint64_t hash_id { 0 };

	bool is_valid { true };
	std::string err;
};


#endif // MAIN_FILEDATA_HPP
