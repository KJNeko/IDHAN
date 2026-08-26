#pragma once
#include <expected>
#include <memory>
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
struct MetadataInfoArchive;
} // namespace idhan

namespace idhan::modules
{
class CallInput;
class RemoteModule;
} // namespace idhan::modules

namespace idhan::metadata
{

struct RecordInfoBatch
{
	Json::Value records { Json::arrayValue };
	std::vector< RecordID > missing {};
};

//! Builds the info object for every id in \p record_ids using one query per metadata table.
//!
//! Nothing here fails: an unknown id lands in \c missing, a record with no file stops at its hash, an
//! unparsed record is marked \c parsed:false, and a simple mime type with no table of its own keeps
//! its basic fields. Parsing is never triggered; callers that want it call parseAndUpdateRecordMetadata first.
[[nodiscard]] drogon::Task< RecordInfoBatch > collectRecordInfo( std::vector< RecordID > record_ids, DbClientPtr db );

[[nodiscard]] drogon::Task< std::shared_ptr< modules::RemoteModule > > findBestParser( MimeID mime_id );

ExpectedTask< void > parseAndUpdateRecordMetadata( RecordID record_id, DbClientPtr db );

ExpectedTask< void > parseAndUpdateRecordMetadata(
	RecordID record_id,
	MimeID mime_id,
	std::shared_ptr< const modules::CallInput > input,
	DbClientPtr db );

ExpectedTask< MetadataInfo > parseMetadata( RecordID record_id, DbClientPtr db );

ExpectedTask< MetadataInfo > parseMetadata(
	RecordID record_id,
	MimeID mime_id,
	std::shared_ptr< const modules::CallInput > input );

ExpectedTask< void > updateRecordMetadata( RecordID record_id, DbClientPtr db, MetadataInfo metadata );

[[nodiscard]] drogon::Task< MetadataInfo > getMetadata( RecordID record_id, DbClientPtr db );

//! Adds the members an archive thumbnailer or generator reads out of its call's extra json: the
//! encrypted flag, plus one sha256-hex to entry path member per contained file.
void applyArchiveEntries( Json::Value& extra, const MetadataInfoArchive& archive );

//! Overload sourcing the entries from archive_map. Adds nothing when the record is not an archive.
drogon::Task< void > applyArchiveEntries( Json::Value& extra, RecordID record_id, DbClientPtr db );

} // namespace idhan::metadata
