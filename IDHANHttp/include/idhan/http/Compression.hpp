#pragma once
#include <proxygen/lib/http/HTTPHeaders.h>
#include <proxygen/lib/http/coro/HTTPSourceHolder.h>

#include <expected>
#include <memory>
#include <optional>
#include <string>

#include "HTTPConfig.hpp"

namespace idhan::http
{
enum class ContentCoding
{
	identity,
	gzip,
	zstd,
	brotli
};

struct Representation
{
	ContentCoding coding;
	std::string etag;
	std::optional< std::uint64_t > content_length;
};

class Compression
{
  public:

	[[nodiscard]] static std::expected< Representation, std::uint16_t > negotiate(
		const proxygen::HTTPHeaders&,
		std::string_view content_type,
		std::optional< std::uint64_t > size,
		bool range,
		bool brotli_available );
	[[nodiscard]] static proxygen::coro::HTTPSourceHolder encode(
		proxygen::coro::HTTPSourceHolder,
		Representation,
		HTTPConfig,
		bool flush_events );
};
} // namespace idhan::http
