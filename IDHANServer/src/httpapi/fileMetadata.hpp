#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <vector>

#include "IDHANTypes.hpp"
#include "filesystem/VerifiedRecordPath.hpp"
#include "idhan/http/routing/ReflectedRoutes.hpp"
#include "idhan/http/routing/RouteSpec.hpp"
#include "recordValidator.hpp"

namespace idhan::db
{
class DBContext;
}

namespace idhan::httpapi
{

[[nodiscard]] http::HTTPTask< std::expected< void, http::Response > > parseAndStoreRecordMetadata(
	db::DBContext& db,
	RecordID record_id );

[[nodiscard]] http::HTTPTask< std::expected< filesystem::VerifiedRecordPath, http::Response > > recordFilePathForScan(
	db::DBContext& db,
	RecordID record_id );

struct RecordInfoResponse
{
	struct Hashes
	{
		std::string sha256;
	};

	struct Tag
	{
		std::string domain;
		std::string text;
	};

	RecordID record_id;
	Hashes hashes;
	std::vector< ArchiveID > archive_ids;
	std::vector< Tag > tags;
	std::vector< std::string > urls;
	http::possiblyMissing< std::int64_t > size {};
	http::possiblyMissing< std::string > mime {};
	http::possiblyMissing< std::string > extension {};
	http::possiblyMissing< std::int64_t > file_mtime {};
	http::possiblyMissing< std::int64_t > file_ctime {};
	http::possiblyMissing< bool > parsed {};
	http::possiblyMissing< std::string > simple_type {};
	//! Parser modules define the fields inside this metadata object.
	http::possiblyMissing< http::JsonDocument > extra {};
	http::possiblyMissing< std::uint32_t > width {};
	http::possiblyMissing< std::uint32_t > height {};
	http::possiblyMissing< std::uint32_t > channels {};
	http::possiblyMissing< std::string > phash {};
	http::possiblyMissing< bool > has_exif {};
	http::possiblyMissing< bool > has_gps {};
	http::possiblyMissing< bool > has_xmp {};
	http::possiblyMissing< bool > has_iptc {};
	http::possiblyMissing< bool > has_icc_profile {};
	http::possiblyMissing< bool > lossless {};
	http::possiblyMissing< std::uint32_t > quality {};
	http::possiblyMissing< std::string > chroma_subsampling {};
	http::possiblyMissing< bool > progressive {};
	http::possiblyMissing< std::string > comment {};
	http::possiblyMissing< std::uint32_t > layers {};
	http::possiblyMissing< double > duration {};
	http::possiblyMissing< std::uint32_t > bitrate {};
	http::possiblyMissing< double > framerate {};
	http::possiblyMissing< bool > has_audio {};
	http::possiblyMissing< std::uint32_t > sample_rate {};
	http::possiblyMissing< std::uint32_t > frame_count {};
	http::possiblyMissing< bool > loops {};
	http::possiblyMissing< ArchiveID > archive_id {};
	http::possiblyMissing< bool > encrypted {};
	http::possiblyMissing< std::int64_t > file_count {};
};

struct RecordInfoBatchResponse
{
	std::vector< RecordInfoResponse > records;
	std::vector< RecordID > missing;
};

struct FetchRecordMetadataBatchBody
{
	[[= http::Minimum { 1 }]] std::vector< RecordID > record_ids;
};

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::GET, "/records/{record_id}/info"}]]
[[= http::Group{"Records"}]]
[[= http::Validation< http::validation::PositiveInteger< RecordID > >{"record_id"}]]
[[= http::Before{&recordExistsValidation}]]
[[= http::Endpoint{proxygen::HTTPMethod::GET, "/records/{record_id}/metadata"}]]
http::HTTPTask< http::JsonResult< RecordInfoResponse > > fetchRecordInfo( db::DBContext& db, http::RequestContext context );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::POST, "/records/metadata"}]]
[[= http::Group{"Records"}]]
http::HTTPTask< http::JsonResult< RecordInfoBatchResponse > > fetchRecordMetadataBatch( db::DBContext& db, http::JsonBody< FetchRecordMetadataBatchBody > body );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::POST, "/records/{record_id}/metadata/scan"}]]
[[= http::Group{"Records"}]]
[[= http::Validation< http::validation::PositiveInteger< RecordID > >{"record_id"}]]
[[= http::Before{&recordExistsValidation}]]
http::HTTPTask< http::JsonResult< RecordInfoResponse > > scanRecordMetadata( db::DBContext& db, http::RequestContext context );
// clang-format on

} // namespace idhan::httpapi
