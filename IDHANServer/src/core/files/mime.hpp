//
// Created by kj16609 on 11/15/24.
//
#pragma once
#include <expected>
#include <string>

#include "IDHANTypes.hpp"
#include "drogon/HttpResponse.h"
#include "drogon/orm/DbClient.h"

namespace idhan::mime
{

//! Searches for a mime type in the database
std::optional< MimeID > searchMimeType( const std::string& name, drogon::orm::DbClientPtr db );

//! Registers a new mime type in the database
MimeID registerMimeType( const std::string& name, drogon::orm::DbClientPtr db );

//! Returns the mime id assocaited with a given record.
//! @throws NoFileInfo if the record has no file info
drogon::Task< std::expected< MimeID, drogon::HttpResponsePtr > >
	getMimeIDFromRecord( RecordID id, drogon::orm::DbClientPtr db );

struct FileMimeInfo
{
	MimeID m_id;
	std::string extension;
};

//! Returns the mime info for a given mime ID
//! @throws NoMimeRecord if the mime id is invalid or does not exist
drogon::Task< std::expected< FileMimeInfo, drogon::HttpResponsePtr > >
	getMime( MimeID mime_id, drogon::orm::DbClientPtr db );

//! @thorws NoFileInfo if there is no file info for the given record
//! @throws NoMimeRecord if the mime id is invalid or does not exist.
drogon::Task< std::expected< FileMimeInfo, drogon::HttpResponsePtr > >
	getRecordMime( RecordID record_id, drogon::orm::DbClientPtr db );

} // namespace idhan::mime
