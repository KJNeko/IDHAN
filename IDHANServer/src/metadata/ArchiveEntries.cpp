#include "ArchiveEntries.hpp"

#include "MetadataInfo.hpp"
#include "crypto/simpleHasher.hpp"

namespace idhan::metadata
{
void applyArchiveEntries( Json::Value& extra, const MetadataInfoArchive& archive )
{
	extra[ "encrypted" ] = archive.encrypted;
	for ( const auto& [ hash, path ] : archive.contained_records ) extra[ crypto::toHex( hash ) ] = path;
}
} // namespace idhan::metadata
