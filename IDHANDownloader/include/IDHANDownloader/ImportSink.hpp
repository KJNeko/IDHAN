#pragma once

#include <json/value.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "SessionObserver.hpp"

namespace idhan::downloader
{

struct ImportRequest
{
	// Identity reserved by onImportQueued, distinct from the parent parser work.
	WorkID work {};
	// Storage form; query ordering follows the URL class without changing the HTTP request.
	std::string url {};
	std::string source_url {};
	Json::Value options {};
	std::uint64_t host_tag {};
	// A URL alone identifies only a GET response without a request body.
	bool direct_eligible { true };
	// Whether an explicit force setting allows replacing a deliberately deleted record.
	bool force_download {};
	// Host-scoped comparison policy; never removes parameters from the request itself.
	std::vector< std::string > ephemeral_query {};
};

struct ImportMetadata
{
	std::uint64_t size {};
	std::string content_type {};
	std::string filename {};
	std::string final_url {};
};

struct ImportResult
{
	RecordID record_id {};
	std::string note {};
};

//! Methods run on a lane IO thread and must not block or access session state.
class ImportSink
{
  public:

	virtual ~ImportSink() = default;

	virtual std::expected< void, std::string > write( std::span< const std::byte > bytes ) = 0;
	virtual std::expected< ImportResult, std::string > finish( const ImportMetadata& metadata ) = 0;
	//! Discards partial output after failure.
	virtual void abort() = 0;
};

} // namespace idhan::downloader
