#pragma once

#include <folly/coro/Task.h>

#include <expected>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace idhan::downloader
{

struct ParserHost
{
	std::string host {};
	std::string parser {};
	bool auto_discovered {};
};

class ParserHostPersistence
{
  public:

	virtual ~ParserHostPersistence() = default;
	virtual folly::coro::Task< std::expected< std::vector< ParserHost >, std::string > > loadAll() = 0;
	// Existing mappings win, including their user-supplied provenance.

	virtual folly::coro::Task< std::expected< ParserHost, std::string > > discover(
		std::string host,
		std::string parser ) = 0;
};

} // namespace idhan::downloader
