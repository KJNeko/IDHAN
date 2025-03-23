//
// Created by kj16609 on 3/20/25.
//
#pragma once

#include <chrono>

#include "IDHANTypes.hpp"
#include "drogon/drogon.h"

namespace idhan
{

struct FileInfo
{
	std::size_t size;
	MimeID mime_id;
	std::string extension;
	std::chrono::time_point< std::chrono::system_clock > store_time;
};

//! Populates a FileInfo struct with information from the data
drogon::Task< FileInfo > gatherFileInfo( const std::byte* data, const std::size_t size, drogon::orm::DbClientPtr db );
drogon::Task< void > setFileInfo( const RecordID record_id, const FileInfo& info, drogon::orm::DbClientPtr db );

} // namespace idhan