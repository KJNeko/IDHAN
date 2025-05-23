//
// Created by kj16609 on 3/20/25.
//
#pragma once

#include <chrono>

#include "IDHANTypes.hpp"
#include "drogon/drogon.h"

namespace idhan
{
class Data;

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
drogon::Task< FileInfo > gatherFileInfo( std::shared_ptr< Data > data, drogon::orm::DbClientPtr db );

drogon::Task< void > setFileInfo( RecordID record_id, const FileInfo& info, drogon::orm::DbClientPtr db );

} // namespace idhan