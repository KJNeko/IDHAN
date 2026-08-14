#pragma once
#include <expected>

#include "IDHANTypes.hpp"
#include "db/dbTypes.hpp"
#include "drogon/HttpResponse.h"
#include "drogon/utils/coroutine.h"
#include "threading/ExpectedTask.hpp"

namespace idhan
{
class FileMappedData;
struct MetadataInfo;
} // namespace idhan

namespace idhan::modules
{
class RemoteModule;
}

namespace idhan::metadata
{

ExpectedTask< void > addFileSpecificInfo( Json::Value& root, RecordID record_id, DbClientPtr db );

[[nodiscard]] drogon::Task< std::shared_ptr< modules::RemoteModule > > findBestParser( std::string mime_name );

ExpectedTask< void > tryParseRecordMetadata( RecordID record_id, DbClientPtr db );

ExpectedTask< MetadataInfo > parseMetadata( RecordID record_id, DbClientPtr db );

ExpectedTask< void > updateRecordMetadata( RecordID record_id, DbClientPtr db, MetadataInfo metadata );

[[nodiscard]] drogon::Task< MetadataInfo > getMetadata( RecordID record_id, DbClientPtr db );

} // namespace idhan::metadata
