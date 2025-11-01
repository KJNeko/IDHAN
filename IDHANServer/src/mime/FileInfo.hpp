//
// Created by kj16609 on 3/20/25.
//
#pragma once

#include <chrono>
#include <expected>

#include "IDHANTypes.hpp"
#include "api/APIAuth.hpp"
#include "db/dbTypes.hpp"
#include "drogon/drogon.h"

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
	gatherFileInfo( FileIOUring io_uring, DbClientPtr db );

drogon::Task<> setFileInfo( RecordID record_id, FileInfo info, DbClientPtr db );

} // namespace idhan
