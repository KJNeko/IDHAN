#pragma once

#include <expected>
#include <optional>
#include <string>

#include "IDHANTypes.hpp"
#include "idhan/runtime/Task.hpp"

namespace idhan::db
{
class DBContext;
}

namespace idhan::downloader::rows
{
template < typename T >
using DbResult = runtime::Task< std::expected< T, std::string > >;

DbResult< DownloadSessionUrlID > insert(
	DownloadSessionID session_id,
	DownloadSessionUrlID parent_row,
	std::string url,
	std::string state,
	std::string message,
	db::DBContext& db );
DbResult< void > mark( DownloadSessionUrlID row_id, std::string target, std::string error, db::DBContext& db );
DbResult< void > addError(
	DownloadSessionID session_id,
	DownloadSessionUrlID row_id,
	std::string url,
	std::string lane,
	std::optional< std::int32_t > status,
	std::string message,
	db::DBContext& db );
DbResult< void > completeImport( DownloadSessionUrlID row_id, RecordID record, std::string note, db::DBContext& db );
} // namespace idhan::downloader::rows
