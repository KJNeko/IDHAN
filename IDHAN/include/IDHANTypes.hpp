#pragma once

#include <cstdint>
#include <string_view>

namespace idhan::types
{
using SmallInt = std::int16_t;
#define SMALLINT_PG_TYPE_NAME "SMALLINT"

using Integer = std::int32_t;
#define INTEGER_PG_TYPE_NAME "INTEGER"

using BigInt = std::int64_t;
#define BIGINT_PG_TYPE_NAME "BIGINT"

using Int = Integer;

using RecordID = Integer;
#define RECORD_PG_TYPE_NAME INTEGER_PG_TYPE_NAME

using ArchiveID = Integer;

using NamespaceID = Integer;
#define NAMESPACE_ID_PG_TYPE_NAME INTEGER_PG_TYPE_NAME
using SubtagID = Integer;
#define SUBTAG_ID_PG_TYPE_NAME INTEGER_PG_TYPE_NAME
using TagID = Integer;
#define TAG_PG_TYPE_NAME INTEGER_PG_TYPE_NAME

using ClusterID = SmallInt;
#define CLUSTER_ID_PG_TYPE_NAME SMALLINT_PG_TYPE_NAME

using MimeID = Integer;
#define MIME_PG_TYPE_NAME INTEGER_PG_TYPE_NAME

using TagDomainID = SmallInt;
#define TAG_DOMAIN_PG_TYPE_NAME SMALLINT_PG_TYPE_NAME
using FileDomainID = SmallInt;
#define FILE_DOMAIN_PG_TYPE_NAME SMALLINT_PG_TYPE_NAME

using UrlID = Integer;
#define URL_PG_TYPE_NAME INTEGER_PG_TYPE_NAME
using UrlDomainID = Integer;
#define URL_DOMAIN_PG_TYPE_NAME INTEGER_PG_TYPE_NAME

using CursorID = Integer;

using NoteID = Integer;
#define NOTE_PG_TYPE_NAME INTEGER_PG_TYPE_NAME

enum class SimpleMimeType : std::uint16_t
{
	NONE = 0,
	IMAGE_TYPE = 1,
	VIDEO = 2,
	ANIMATION = 3,
	AUDIO = 4,
	ARCHIVE = 5,
	IMAGE_PROJECT = 6
};

constexpr TagID INVALID_TAG_ID { 0 };

using JobID = std::uint64_t;

} // namespace idhan::types

namespace idhan
{
using namespace idhan::types;
}
