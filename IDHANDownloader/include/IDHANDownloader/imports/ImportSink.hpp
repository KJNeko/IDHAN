#pragma once

#include <folly/coro/Task.h>
#include <json/value.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "IDHANDownloader/session/SessionObserver.hpp"

namespace idhan::downloader
{

// Downloader -> IDHAN
struct ImportRequest
{
	WorkID work {};
	// import() receives the direct file URL chosen by the parser.
	std::string url {};
	Json::Value options {};
	DownloadSessionID session_id {};
	bool force_download {};
	// When present, finish the import using this record without writing file bytes.
	std::optional< RecordID > stored_record {};
};

// Downloader -> IDHAN
struct ImportMetadata
{
	std::uint64_t size {};
	std::string content_type {};
	std::string filename {};
	// Final HTTP file URL; empty when importDirect() supplies the bytes.
	std::string effective_url {};
};

// IDHAN -> Downloader
struct ImportResult
{
	RecordID record_id {};
	std::string note {};
	bool existing {};
};

class ImportSink
{
  public:

	virtual ~ImportSink() = default;

	virtual folly::coro::Task< std::expected< void, std::string > > write( std::span< const std::byte > bytes ) = 0;

	virtual folly::coro::Task< std::expected< ImportResult, std::string > > finish( ImportMetadata metadata ) = 0;

	virtual void abort() = 0;
};

} // namespace idhan::downloader
