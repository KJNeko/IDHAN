//
// Created by kj16609 on 7/20/22.
//


#pragma once
#ifndef IDHAN_IMPORTER_HPP
#define IDHAN_IMPORTER_HPP


#include <filesystem>
#include <vector>

#include "database/tags/groups.hpp"
#include "database/tags/subtags.hpp"

#include "database/files/files.hpp"


enum class ImportResult
{
	SUCCESS = 0, ALREADY_IMPORTED, FILE_NOT_FOUND, UNKNOWN_ERR
};

using ImportResultOutput = std::pair< ImportResult, uint64_t >;

ImportResultOutput importToDB( const std::filesystem::path& path );


#endif //IDHAN_IMPORTER_HPP
