//
// Created by kj16609 on 6/12/25.
//
#pragma once

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
#include <drogon/drogon.h>
#pragma GCC diagnostic pop

#include <expected>

#include "IDHANTypes.hpp"

namespace idhan
{
class FileMappedData;
struct MetadataInfo;
} // namespace idhan

namespace idhan::api
{

//! Triggers the metadata parsing for a record and updates it
drogon::Task< std::expected< void, drogon::HttpResponsePtr > >
	tryParseRecordMetadata( RecordID record_id, drogon::orm::DbClientPtr db );

//! Returns the metadata for a given record after parsing the file
drogon::Task< std::expected< MetadataInfo, drogon::HttpResponsePtr > >
	parseMetadata( RecordID record_id, drogon::orm::DbClientPtr db );

//! Updates the record metadata for a record
drogon::Task< std::expected< void, drogon::HttpResponsePtr > >
	updateRecordMetadata( RecordID record_id, drogon::orm::DbClientPtr db, MetadataInfo metadata );

drogon::Task< MetadataInfo > getMetadata( const RecordID record_id, drogon::orm::DbClientPtr db );

} // namespace idhan::api
