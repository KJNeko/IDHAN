//
// Created by kj16609 on 3/20/25.
//
#pragma once

#include <chrono>
#include <expected>

#include "IDHANTypes.hpp"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Weffc++"
#pragma GCC diagnostic ignored "-Wredundant-tags"
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wnoexcept"
#pragma GCC diagnostic ignored "-Wredundant-decls"
#pragma GCC diagnostic ignored "-Wuseless-cast"
#pragma GCC diagnostic ignored "-Wnoexcept"
#pragma GCC diagnostic ignored "-Wswitch-enum"
#pragma GCC diagnostic ignored "-Wshadow"

#include "drogon/drogon.h"
#pragma GCC diagnostic pop

#include "api/APIAuth.hpp"

namespace idhan
{
class FileIOUring;
class FileMappedData;

namespace constants
{
constexpr MimeID INVALID_MIME_ID { 0 };
}

struct FileInfo
{
	std::size_t size;
	MimeID mime_id;
	std::string extension;
	std::chrono::time_point< std::chrono::system_clock > store_time;
};

//! Populates a FileInfo struct with information from the data
drogon::Task< std::expected< FileInfo, drogon::HttpResponsePtr > >
	gatherFileInfo( FileIOUring io_uring, drogon::orm::DbClientPtr db );

drogon::Task<> setFileInfo( RecordID record_id, FileInfo info, drogon::orm::DbClientPtr db );

} // namespace idhan