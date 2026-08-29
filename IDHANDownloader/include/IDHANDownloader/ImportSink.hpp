#pragma once

#include <json/value.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>

#include "SessionObserver.hpp"

namespace idhan::downloader
{

struct ImportRequest
{
	WorkID work {};
	std::string url {};
	std::string source_url {};
	Json::Value options {};
	std::uint64_t host_tag {};
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

class ImportSinkFactory
{
  public:

	virtual ~ImportSinkFactory() = default;

	virtual std::expected< std::unique_ptr< ImportSink >, std::string > open( const ImportRequest& request ) = 0;
};

} // namespace idhan::downloader
