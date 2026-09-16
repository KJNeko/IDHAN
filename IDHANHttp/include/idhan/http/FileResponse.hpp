#pragma once
#include <folly/File.h>

#include <chrono>
#include <string>

#include "RequestContext.hpp"
#include "Response.hpp"

namespace idhan::http
{

struct FileMetadata
{
	std::uint64_t size;
	std::chrono::system_clock::time_point modified, changed;
	std::string etag, content_type, filename;
	bool immutable { false };
	bool attachment { false };
};

class FileResponse
{
  public:

	[[nodiscard]] static HTTPTask< Response > create( const RequestContext&, folly::File, FileMetadata );
};

} // namespace idhan::http
