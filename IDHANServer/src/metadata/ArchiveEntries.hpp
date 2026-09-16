#pragma once

#include <json/value.h>

namespace idhan
{
struct MetadataInfoArchive;
}

namespace idhan::metadata
{
void applyArchiveEntries( Json::Value& extra, const MetadataInfoArchive& archive );
}
