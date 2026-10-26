#pragma once

#include "IDHANTypes.hpp"

namespace idhan::downloader
{

class DownloadSessionEventHub
{
  public:

	static DownloadSessionEventHub& instance();

	void notify( DownloadSessionID session_id );
	void notifyRemoved( DownloadSessionID session_id );
};

} // namespace idhan::downloader
