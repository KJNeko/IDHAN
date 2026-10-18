#include "ArchiveEntries.hpp"

#include "MetadataInfo.hpp"
#include "idhan/SHA256.hpp"

namespace idhan::metadata
{
void applyArchiveEntries( Json::Value& extra, const MetadataInfoArchive& archive )
{
	extra[ "encrypted" ] = archive.encrypted;
	for ( const auto& [ hash, path ] : archive.contained_records )
		extra[ idhan::SHA256::fromBuffer( hash ).hex() ] = path;
}
} // namespace idhan::metadata
