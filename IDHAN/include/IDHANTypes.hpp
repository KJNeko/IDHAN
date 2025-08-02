#pragma once

#include <cstdint>

namespace idhan
{

using SmallInt = std::int16_t;
using Integer = std::int32_t;
using BigInt = std::int64_t;

using RecordID = Integer;

using NamespaceID = Integer;
using SubtagID = Integer;
using TagID = Integer;

using ClusterID = SmallInt;

using MimeID = Integer;

using TagDomainID = SmallInt;
using FileDomainID = SmallInt;

using UrlID = Integer;
using UrlDomainID = Integer;

enum class SimpleMimeType : std::uint16_t
{
	NONE = 0,
	IMAGE = 1,
	VIDEO = 2,
	ANIMATION = 3,
	AUDIO = 4
};

constexpr TagID INVALID_TAG_ID { 0 };

} // namespace idhan
