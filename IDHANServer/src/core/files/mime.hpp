#pragma once
#include <expected>
#include <optional>
#include <string>

#include "IDHANTypes.hpp"
#include "db/dbTypes.hpp"
#include "drogon/HttpResponse.h"
#include "drogon/orm/DbClient.h"

namespace idhan::mime
{
[[nodiscard]] std::optional< MimeID > searchMimeType( const std::string& name, DbClientPtr db );

struct RecordMimeIDLookup
{
	bool has_file_info;
	std::optional< MimeID > mime_id;
};

[[nodiscard]] drogon::Task< RecordMimeIDLookup > lookupRecordMimeID( RecordID id, DbClientPtr db );

//! @throws NoFileInfo if the record has no file info
[[nodiscard]] drogon::Task< std::expected< MimeID, drogon::HttpResponsePtr > > getMimeIDFromRecord(
	RecordID id,
	DbClientPtr db );

struct FileMimeInfo
{
	MimeID m_id;
	std::string extension;
};

[[nodiscard]] drogon::Task< std::optional< FileMimeInfo > > findMime( MimeID mime_id, DbClientPtr db );

//! @throws NoMimeRecord if the mime id is invalid or does not exist
[[nodiscard]] drogon::Task< std::expected< FileMimeInfo, drogon::HttpResponsePtr > > getMime(
	MimeID mime_id,
	DbClientPtr db );

//! @throws NoFileInfo if there is no file info for the given record
//! @throws NoMimeRecord if the mime id is invalid or does not exist.
[[nodiscard]] drogon::Task< std::expected< FileMimeInfo, drogon::HttpResponsePtr > > getRecordMime(
	RecordID record_id,
	DbClientPtr db );
} // namespace idhan::mime
