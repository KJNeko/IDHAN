#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "IDHANTypes.hpp"
#include "idhan/db/DBContext.hpp"
#include "idhan/runtime/Task.hpp"

namespace idhan::httpapi::metadata
{

struct RecordTag
{
	std::string domain {};
	std::string text {};
};

struct RecordInfoEntry
{
	RecordID record_id {};
	std::string sha256_hex {};
	std::vector< ArchiveID > archive_ids {};

	//! Applied rather than stored: aliases, siblings and parents are already resolved.
	std::vector< RecordTag > tags {};
	std::vector< std::string > urls {};

	std::optional< std::int64_t > size {};
	std::optional< std::string > mime {};
	std::optional< std::string > extension {};
	std::optional< std::int64_t > file_mtime {};
	std::optional< std::int64_t > file_ctime {};

	std::optional< bool > parsed {};

	std::optional< std::string > simple_type {};
	std::optional< std::string > extra_json {};

	std::optional< std::uint32_t > width {};
	std::optional< std::uint32_t > height {};
	std::optional< std::uint32_t > channels {};

	std::optional< std::string > phash_hex {};
	std::optional< bool > has_exif {};
	std::optional< bool > has_gps {};
	std::optional< bool > has_xmp {};
	std::optional< bool > has_iptc {};
	std::optional< bool > has_icc_profile {};
	std::optional< bool > lossless {};
	std::optional< std::uint32_t > quality {};
	std::optional< std::string > chroma_subsampling {};
	std::optional< bool > progressive {};
	std::optional< std::string > comment {};

	std::optional< std::uint32_t > layers {};

	std::optional< double > duration {};
	std::optional< std::uint32_t > bitrate {};
	std::optional< double > framerate {};
	std::optional< bool > has_audio {};
	std::optional< std::uint32_t > sample_rate {};
	std::optional< std::uint32_t > frame_count {};
	std::optional< bool > loops {};

	std::optional< ArchiveID > archive_id {};
	std::optional< bool > encrypted {};
	std::optional< std::int64_t > file_count {};
};

struct RecordInfoBatch
{
	std::vector< RecordInfoEntry > records {};
	std::vector< RecordID > missing {};
};

//! batch.records mirrors record_ids exactly: same order, same repeats. Ids not found go into
//! batch.missing instead, in the same relative order.
[[nodiscard]] runtime::Task< RecordInfoBatch > collectRecordInfo(
	std::vector< RecordID > record_ids,
	db::DBContext& db );

} // namespace idhan::httpapi::metadata
