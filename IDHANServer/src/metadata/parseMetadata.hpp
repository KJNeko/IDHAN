//
// Created by kj16609 on 6/12/25.
//
#pragma once

#include <drogon/drogon.h>

#include "IDHANTypes.hpp"
#include "api/helpers/ExpectedTask.hpp"
#include "db/dbTypes.hpp"

namespace idhan
{
class MetadataModuleI;
class FileMappedData;
struct MetadataInfo;
} // namespace idhan

namespace idhan::api
{

drogon::Task< std::shared_ptr< MetadataModuleI > > findBestParser( std::string mime_name );

//! Triggers the metadata parsing for a record and updates it
ExpectedTask< void > tryParseRecordMetadata( RecordID record_id, DbClientPtr db );

//! Returns the metadata for a given record after parsing the file
ExpectedTask< MetadataInfo > parseMetadata( RecordID record_id, DbClientPtr db );

//! Updates the record metadata for a record
ExpectedTask< void > updateRecordMetadata( RecordID record_id, DbClientPtr db, MetadataInfo metadata );

drogon::Task< MetadataInfo > getMetadata( RecordID record_id, DbClientPtr db );

} // namespace idhan::api
