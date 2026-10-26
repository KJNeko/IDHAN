#pragma once

#include <curl/curl.h>

namespace idhan::downloader
{

class IoSource
{
  public:

	virtual ~IoSource() = default;

	[[nodiscard]] virtual CURLM* multi() = 0;
	virtual void onProgress() = 0;
};

} // namespace idhan::downloader
