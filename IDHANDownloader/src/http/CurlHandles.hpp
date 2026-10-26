#pragma once

#include <curl/curl.h>

#include <memory>

namespace idhan::downloader
{

struct CurlEasyDeleter
{
	void operator()( CURL* easy ) const noexcept { curl_easy_cleanup( easy ); }
};

struct CurlMultiDeleter
{
	void operator()( CURLM* multi ) const noexcept { curl_multi_cleanup( multi ); }
};

struct CurlHeaderListDeleter
{
	void operator()( curl_slist* headers ) const noexcept { curl_slist_free_all( headers ); }
};

using CurlEasyPtr = std::unique_ptr< CURL, CurlEasyDeleter >;
using CurlMultiPtr = std::unique_ptr< CURLM, CurlMultiDeleter >;
using CurlHeaderListPtr = std::unique_ptr< curl_slist, CurlHeaderListDeleter >;

//! curl_slist_append returns the new head and leaves the old list intact on failure.
inline bool appendHeader( CurlHeaderListPtr& headers, const char* line )
{
	curl_slist* appended { curl_slist_append( headers.get(), line ) };

	if ( appended == nullptr ) return false;

	[[maybe_unused]] const auto previous { headers.release() };
	headers.reset( appended );
	return true;
}

} // namespace idhan::downloader
