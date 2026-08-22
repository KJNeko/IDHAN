#pragma once
#include <expected>
#include <vector>

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

//! The result of one info fetch: an array of record objects plus the ids that had no record.
struct RecordInfoBatch
{
	Json::Value records { Json::arrayValue };
	std::vector< RecordID > missing {};
};

//! Builds the info object for every id in \p record_ids using one query per metadata table.
//!
//! Nothing here fails: an unknown id lands in \c missing, a record with no file stops at its hash, an
//! unparsed record is marked \c parsed:false, and a simple mime type with no table of its own keeps
//! its basic fields. Parsing is never triggered; callers that want it call tryParseRecordMetadata first.
[[nodiscard]] drogon::Task< RecordInfoBatch > collectRecordInfo( std::vector< RecordID > record_ids, DbClientPtr db );

[[nodiscard]] drogon::Task< std::shared_ptr< modules::RemoteModule > > findBestParser( MimeID mime_id );

ExpectedTask< void > tryParseRecordMetadata( RecordID record_id, DbClientPtr db );

ExpectedTask< MetadataInfo > parseMetadata( RecordID record_id, DbClientPtr db );

ExpectedTask< void > updateRecordMetadata( RecordID record_id, DbClientPtr db, MetadataInfo metadata );

[[nodiscard]] drogon::Task< MetadataInfo > getMetadata( RecordID record_id, DbClientPtr db );

} // namespace idhan::metadata
