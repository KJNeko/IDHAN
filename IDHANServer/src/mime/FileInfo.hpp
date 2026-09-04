#pragma once

#include <chrono>
#include <expected>
#include <memory>
#include <optional>

#include "IDHANTypes.hpp"
#include "MimeIDs.hpp"
#include "api/APIAuth.hpp"
#include "db/dbTypes.hpp"
#include "drogon/drogon.h"

namespace idhan
{
class FileIOUring;
class FileMappedData;

namespace constants
{
constexpr MimeID INVALID_MIME_ID { mime_ids::INVALID };
}

struct FileInfo
{
	std::size_t size;
	MimeID mime_id;
	std::string extension;
	//! When the cluster took ownership of the file, not a property of the file itself.
	std::chrono::time_point< std::chrono::system_clock > store_time;
	std::chrono::time_point< std::chrono::system_clock > file_mtime;
	std::optional< std::chrono::time_point< std::chrono::system_clock > > file_ctime;
};

[[nodiscard]] drogon::Task< FileInfo > gatherFileInfo( std::shared_ptr< FileIOUring > io_uring );

drogon::Task<> setFileInfo( RecordID record_id, FileInfo info, DbClientPtr db );

} // namespace idhan
