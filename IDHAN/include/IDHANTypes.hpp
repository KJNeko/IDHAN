#pragma once

#include <cstdint>

namespace idhan
{

using SmallInt = std::uint16_t;
using Integer = std::uint32_t;
using BigInt = std::uint64_t;

using RecordID = Integer;

using NamespaceID = Integer;
using SubtagID = Integer;
using TagID = Integer;

using ClusterID = SmallInt;

using MimeID = Integer;

using TagDomainID = SmallInt;
using FileDomainID = SmallInt;
} // namespace idhan
