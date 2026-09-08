#pragma once

#include <drogon/orm/DbClient.h>
#include <drogon/utils/coroutine.h>

#include <expected>
#include <optional>
#include <string>

#include "IDHANTypes.hpp"

namespace idhan::downloader::rows
{
// Database errors stay independent of HTTP responses and caller transaction policy.
template < typename T >
using Result = drogon::Task< std::expected< T, std::string > >;

Result< DownloadSessionUrlID > insert(
	DownloadSessionID session_id,
	DownloadSessionUrlID parent_row,
	std::string url,
	std::string state,
	std::string message,
	drogon::orm::DbClientPtr db );
Result< void > mark( DownloadSessionUrlID row_id, std::string target, std::string error, drogon::orm::DbClientPtr db );
Result< void > addError(
	DownloadSessionID session_id,
	DownloadSessionUrlID row_id,
	std::string url,
	std::string lane,
	std::optional< std::int32_t > status,
	std::string message,
	drogon::orm::DbClientPtr db );
Result< void > completeImport(
	DownloadSessionUrlID row_id,
	RecordID record,
	std::string note,
	drogon::orm::DbClientPtr db );
} // namespace idhan::downloader::rows
