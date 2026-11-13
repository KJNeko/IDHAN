#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "IDHANDownloader/http/HttpVersion.hpp"
#include "IDHANDownloader/imports/ImportSink.hpp"
#include "imports/BufferedImportSink.hpp"
#include "http/HttpMessage.hpp"
#include "http/NetworkActivity.hpp"

namespace idhan::downloader
{
class CookieOverlay;
class CookieStore;

enum class TransferErrorCode : std::uint8_t
{
	INVALID_REQUEST,
	INVALID_URL,
	TRANSPORT,
	RATE_LIMITED,
	TOO_LARGE,
	SINK,
	CANCELLED,
	SHUTDOWN,
};

struct TransferError
{
	TransferErrorCode code {};
	std::string message {};
};

struct CookieContext
{
	CookieOverlay* overlay {};
	CookieStore* store {};
};

struct TransferOptions
{
	std::optional< HttpVersion > http_version {};
	bool follow_redirects { true };
	// Reject missing URLs as soon as the response headers arrive.
	bool stop_on_not_found { true };
	int max_redirects { 10 };
	//! Ignored for streamed responses.
	std::size_t max_response_bytes {};
	//! 0 disables the total timeout, not the connect timeout.
	long timeout_ms {};
	std::string user_agent {};
};

struct TransferHop
{
	std::string url {};
	std::int32_t status {};
};

struct TransferResponse
{
	std::int32_t status {};
	std::string url {};
	HttpHeaders headers {};
	std::string body {};
	std::uint64_t bytes {};
	std::vector< TransferHop > hops {};
	std::optional< ImportResult > import {};
	bool import_pending {};
	// Releases the lane slot after worker-side finalization, limiting queued file buffers.
	std::move_only_function< void() > import_finished {};
	ImportMetadata import_metadata {};
	//! Hops that deliver no file preserve the sink and discard their bodies.
	std::unique_ptr< BufferedImportSink > sink {};
};

using TransferResult = std::expected< TransferResponse, TransferError >;
using TransferCallback = std::move_only_function< void( TransferResult ) >;

struct TransferRequest
{
	std::shared_ptr< NetworkActivity > network {};
	std::shared_ptr< NetworkActivity::Request > progress {};
	HttpMethod method { HttpMethod::GET };
	std::string url {};
	HttpHeaders headers {};
	std::string body {};
	//! Query parameters to redact from diagnostics.
	std::vector< std::string > sensitive_query {};
	TransferOptions options {};
	CookieContext cookies {};
	std::unique_ptr< ImportSink > sink {};
	std::unique_ptr< BufferedImportSink > buffered_sink {};
	ImportRequest import {};
	//! Shared across redirect hops.
	std::shared_ptr< std::atomic_bool > cancellation {};
};

} // namespace idhan::downloader
