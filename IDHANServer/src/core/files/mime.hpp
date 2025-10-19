//
// Created by kj16609 on 11/15/24.
//
#pragma once
#include <expected>
#include <string>

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
#include "drogon/HttpResponse.h"
#include "drogon/orm/DbClient.h"
#pragma GCC diagnostic pop

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
