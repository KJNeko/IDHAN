#include "downloaderCache.hpp"

#include "downloader/DownloadSessionManager.hpp"

namespace idhan::httpapi
{

http::HTTPTask< http::JsonResult< downloader::ParserCacheSnapshot > > parserCacheStats( http::RequestContext )
{
	co_return http::JsonResponse< downloader::ParserCacheSnapshot > {
		downloader::downloadSessionManager().cacheSnapshot()
	};
}

} // namespace idhan::httpapi
