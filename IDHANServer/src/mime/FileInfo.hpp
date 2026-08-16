#pragma once

#include <chrono>
#include <expected>
#include <memory>

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
	std::chrono::time_point< std::chrono::system_clock > modified_time;
};

[[nodiscard]] drogon::Task< std::expected< FileInfo, drogon::HttpResponsePtr > > gatherFileInfo(
	std::shared_ptr< FileIOUring > io_uring,
	DbClientPtr db );

drogon::Task<> setFileInfo( RecordID record_id, FileInfo info, DbClientPtr db );

} // namespace idhan
