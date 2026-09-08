#include "http/IoPool.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <mutex>
#include <thread>

namespace idhan::downloader
{

static std::once_flag curl_initialised {};
static bool curl_http3 {};
static bool curl_http2 {};

void IoPool::initialiseCurl()
{
	std::call_once(
		curl_initialised,
		[]
		{
			const auto code { curl_global_init( CURL_GLOBAL_DEFAULT ) };

			if ( code != CURLE_OK )
			{
				spdlog::error( "downloader http: curl_global_init failed: {}", curl_easy_strerror( code ) );
				return;
			}

			const auto* version { curl_version_info( CURLVERSION_NOW ) };

			if ( version == nullptr ) return;

			curl_http2 = ( version->features & CURL_VERSION_HTTP2 ) != 0;
			curl_http3 = ( version->features & CURL_VERSION_HTTP3 ) != 0;
			spdlog::info(
				"downloader http: libcurl {} (HTTP/2 {}, HTTP/3 {})",
				version->version,
				curl_http2 ? "yes" : "no",
				curl_http3 ? "yes" : "no" );
		} );
}

bool IoPool::supportsHttp3()
{
	initialiseCurl();
	return curl_http3;
}

bool IoPool::supportsHttp2()
{
	initialiseCurl();
	return curl_http2;
}

IoPool::IoPool( const std::size_t count )
{
	initialiseCurl();
	const std::size_t threads {
		count == 0 ? std::max< std::size_t >( 2, std::thread::hardware_concurrency() / 2 ) : count
	};
	m_threads.reserve( threads );

	for ( std::size_t index = 0; index < threads; ++index ) m_threads.emplace_back( std::make_unique< IoThread >() );
}

IoPool::~IoPool()
{
	stop();
}

void IoPool::stop()
{
	for ( auto& thread : m_threads ) thread->stop();
}

IoThread& IoPool::leastLoaded()
{
	auto* best { m_threads.front().get() };

	for ( auto& thread : m_threads )
	{
		if ( thread->load() < best->load() ) best = thread.get();
	}

	return *best;
}

} // namespace idhan::downloader
