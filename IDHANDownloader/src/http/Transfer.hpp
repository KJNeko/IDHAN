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

#include "IDHANDownloader/DownloaderContext.hpp"
#include "IDHANDownloader/ImportSink.hpp"
#include "http/HttpMessage.hpp"

namespace idhan::downloader
{
class CookieOverlay;
class CookieStore;

enum class TransferErrorCode : std::uint8_t
{
	INVALID_REQUEST,
	INVALID_URL,
	TRANSPORT,
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
	int max_redirects { 10 };
	//! Ignored for streamed responses.
	std::size_t max_response_bytes {};
	//! 0 disables the total timeout, not the connect timeout.
	long timeout_ms {};
	std::string user_agent {};
	//! 0 leaves the transfer uncapped.
	std::uint64_t bytes_per_second {};
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
	ImportMetadata import_metadata {};
	//! Hops that deliver no file preserve the sink and discard their bodies.
	std::unique_ptr< ImportSink > sink {};
};

using TransferResult = std::expected< TransferResponse, TransferError >;
using TransferCallback = std::function< void( TransferResult ) >;

struct TransferRequest
{
	HttpMethod method { HttpMethod::GET };
	std::string url {};
	HttpHeaders headers {};
	std::string body {};
	//! Query parameters to redact from diagnostics.
	std::vector< std::string > sensitive_query {};
	TransferOptions options {};
	CookieContext cookies {};
	std::unique_ptr< ImportSink > sink {};
	ImportRequest import {};
	//! Shared across redirect hops.
	std::shared_ptr< std::atomic_bool > cancellation {};
};

} // namespace idhan::downloader
