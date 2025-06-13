//
// Created by kj16609 on 6/12/25.
//
#pragma once
#include <drogon/drogon.h>

#include <expected>

#include "IDHANTypes.hpp"

namespace idhan
{
class FileMappedData;
}

struct MetadataInfo;

namespace idhan::api
{

drogon::Task< std::expected< void, drogon::HttpResponsePtr > >
	updateRecordMetadata( RecordID record_id, drogon::orm::DbClientPtr db, MetadataInfo metadata );

drogon::Task< std::expected< MetadataInfo, drogon::HttpResponsePtr > >
	getMetadata( RecordID record_id, std::shared_ptr< FileMappedData > data, drogon::orm::DbClientPtr db );

drogon::Task< std::expected< void, drogon::HttpResponsePtr > >
	tryParseRecordMetadata( RecordID record_id, drogon::orm::DbClientPtr db );

} // namespace idhan::api
