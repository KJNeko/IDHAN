#include <drogon/drogon.h>
#include <json/json.h>

#include "MetadataInfo.hpp"
#include "crypto/simpleHasher.hpp"
#include "metadata.hpp"

namespace idhan::metadata
{

void applyArchiveEntries( Json::Value& extra, const MetadataInfoArchive& archive )
{
	extra[ "encrypted" ] = archive.encrypted;

	for ( const auto& [ hash, path ] : archive.contained_records ) extra[ crypto::toHex( hash ) ] = path;
}

drogon::Task< void > applyArchiveEntries( Json::Value& extra, const RecordID record_id, DbClientPtr db )
{
	const auto rows { co_await db->execSqlCoro(
		"SELECT meta.encrypted, encode(records.sha256, 'hex') AS hex, map.path "
		"FROM archive_metadata meta "
		"LEFT JOIN archive_map map ON map.archive_id = meta.archive_id "
		"LEFT JOIN records ON records.record_id = map.record_id "
		"WHERE meta.record_id = $1",
		record_id ) };

	for ( const auto& row : rows )
	{
		extra[ "encrypted" ] = row[ "encrypted" ].as< bool >();

		if ( row[ "hex" ].isNull() ) continue;

		extra[ row[ "hex" ].as< std::string >() ] = row[ "path" ].as< std::string >();
	}

	co_return;
}

} // namespace idhan::metadata
