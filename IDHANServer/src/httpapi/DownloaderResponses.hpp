#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "IDHANTypes.hpp"
#include "idhan/http/openapi/RouteDocumentation.hpp"

namespace idhan::httpapi
{

struct[ [= http::SchemaName { "DownloadSession" }] ] DownloadSessionResponse
{
	[[= http::Minimum { 1 }]] DownloadSessionID id;
	std::string name;
	std::int64_t created_at;
	std::int64_t last_used_at;
	[[= http::Minimum { 0 }]] std::int64_t error_count;
	std::uint64_t downloading;
	std::uint64_t waiting;
	std::uint64_t waiting_parsers;
	std::uint64_t waiting_requests;
	std::uint64_t waiting_http_rate;
	std::uint64_t waiting_http_shard;
	std::uint64_t waiting_storage;
	std::uint64_t download_bytes_per_second;
	std::uint64_t downloaded_bytes;
	std::uint64_t average_import_bytes;
	bool is_subscription {};
};

struct DownloaderDeletedResponse
{
	bool deleted { true };
};

struct ParserInstalledResponse
{
	bool installed { true };
};

struct[ [= http::SchemaName { "SubmittedDownloadUrl" }] ] SubmittedDownloadUrlResponse
{
	[[= http::Minimum { 1 }]] DownloadSessionUrlID id;
	std::string url;
	std::string state;
};

struct DownloadUrlResponse
{
	[[= http::Minimum { 1 }]] DownloadSessionUrlID id;
	[[= http::Minimum { 1 }]] std::optional< DownloadSessionUrlID > parent_id;
	std::string url;
	std::string state;
	std::int64_t created_at;
	std::optional< std::int64_t > finished_at;
	std::optional< std::string > error;
	std::optional< std::string > stage;
	[[= http::Minimum { 1 }]] std::optional< RecordID > record_id;
	[[= http::Minimum { 0 }]] std::optional< std::int64_t > size;
};

struct DownloadUrlNodeResponse
{
	[[= http::Minimum { 1 }]] DownloadSessionUrlID id;
	[[= http::Minimum { 1 }]] std::optional< DownloadSessionUrlID > parent_id;
	std::string url;
	std::string state;
	std::int64_t created_at;
	std::optional< std::int64_t > finished_at;
	std::optional< std::string > error;
	std::optional< std::string > stage;
	[[= http::Minimum { 1 }]] std::optional< RecordID > record_id;
	[[= http::Minimum { 0 }]] std::optional< std::int64_t > size;
	std::vector< DownloadUrlNodeResponse > children;
};

struct DownloadUrlFlatResponse
{
	[[= http::Minimum { 1 }]] DownloadSessionUrlID id;
	[[= http::Minimum { 1 }]] std::optional< DownloadSessionUrlID > parent_id;
	std::string url;
	std::string state;
	std::int64_t created_at;
	std::optional< std::int64_t > finished_at;
	std::optional< std::string > error;
	std::optional< std::string > stage;
	[[= http::Minimum { 1 }]] std::optional< RecordID > record_id;
	[[= http::Minimum { 0 }]] std::optional< std::int64_t > size;
	std::vector< DownloadUrlResponse > urls;
	[[= http::Minimum { 1 }]] std::vector< RecordID > record_ids;
};

using DownloadUrlListResponse =
	std::variant< std::vector< DownloadUrlNodeResponse >, std::vector< DownloadUrlFlatResponse > >;

struct SubmittedDownloadSessionResponse
{
	DownloadSessionResponse session;
	DownloadUrlNodeResponse url;
};

struct DownloadSessionRecordsResponse
{
	[[= http::Minimum { 1 }]] std::vector< RecordID > record_ids;
};

struct DownloadErrorStatusResponse
{
	[[= http::Minimum { 0 }]] std::optional< Int > status;
	[[= http::Minimum { 0 }]] std::int64_t count;
};

struct DownloadErrorResponse
{
	[[= http::Minimum { 1 }]] std::int64_t id;
	[[= http::Minimum { 1 }]] std::optional< DownloadSessionUrlID > url_id;
	std::string url;
	[[= http::Minimum { 0 }]] std::optional< Int > status;
	std::string lane;
	std::optional< std::string > message;
	std::int64_t occurred_at;
};

struct DownloadSessionErrorsResponse
{
	std::vector< DownloadErrorStatusResponse > statuses;
	std::vector< DownloadErrorResponse > errors;
};

// Parser option names, metadata extensions, and secrets are supplied by installed scripts.
using DownloaderJsonObject = std::map< std::string, http::JsonDocument >;
using DownloaderSecrets = std::map< std::string, DownloaderJsonObject >;
using ParserOptionValue =
	std::variant< bool, std::int64_t, std::uint64_t, double, std::string, std::vector< std::string > >;

struct ParserOptionResponse
{
	std::string name;
	std::string description;
	std::string type;
	[[= http::Name { "default" }]] ParserOptionValue default_value;
	http::possiblyMissing< double > minimum;
	http::possiblyMissing< bool > integer;
};

struct ParserOptionsResponse
{
	std::string id;
	std::vector< ParserOptionResponse > options;
};

struct DownloadOptionsResponse
{
	bool force_import;
	std::vector< ParserOptionsResponse > parsers;
};

struct ParserMetadataResponse
{
	http::possiblyMissing< std::uint64_t > formatVersion;
	http::possiblyMissing< std::string > sdkVersion;
	std::string id;
	[[= http::Minimum { 1 }]] std::uint64_t version;
	std::vector< ParserOptionResponse > options;
	std::vector< DownloaderJsonObject > handlers;
	std::vector< DownloaderJsonObject > discoveries;
	std::vector< DownloaderJsonObject > urlClasses;
	std::string filename;
};

struct ParserListResponse
{
	std::vector< ParserMetadataResponse > parsers;
};

} // namespace idhan::httpapi
