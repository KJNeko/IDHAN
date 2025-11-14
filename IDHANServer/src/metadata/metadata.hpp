//
// Created by kj16609 on 11/13/25.
//
#pragma once
#include <expected>

#include "IDHANTypes.hpp"
#include "db/dbTypes.hpp"
#include "drogon/HttpResponse.h"
#include "drogon/utils/coroutine.h"
#include "threading/ExpectedTask.hpp"

namespace idhan
{
class MetadataModuleI;
class FileMappedData;
struct MetadataInfo;
} // namespace idhan

namespace idhan::metadata
{

// DB

ExpectedTask< void > addFileSpecificInfo( Json::Value& root, RecordID record_id, DbClientPtr db );

// Parsing

drogon::Task< std::shared_ptr< MetadataModuleI > > findBestParser( std::string mime_name );

//! Triggers the metadata parsing for a record and updates it
ExpectedTask< void > tryParseRecordMetadata( RecordID record_id, DbClientPtr db );

//! Returns the metadata for a given record after parsing the file
ExpectedTask< MetadataInfo > parseMetadata( RecordID record_id, DbClientPtr db );

//! Updates the record metadata for a record
ExpectedTask< void > updateRecordMetadata( RecordID record_id, DbClientPtr db, MetadataInfo metadata );

drogon::Task< MetadataInfo > getMetadata( RecordID record_id, DbClientPtr db );

} // namespace idhan::metadata