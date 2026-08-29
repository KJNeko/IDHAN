#pragma once

#include <curl/curl.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>

#include "http/CurlHandles.hpp"
#include "http/IoPool.hpp"
#include "http/Transfer.hpp"

namespace idhan::downloader
{
class Lane;

class LaneShard final : public IoSource
{
  public:

	struct Active
	{
		CurlEasyPtr easy {};
		CurlHeaderListPtr header_list {};
		std::array< char, CURL_ERROR_SIZE > error {};
		TransferRequest request {};
		TransferResponse response {};
		TransferCallback callback {};
		LaneShard* shard {};
		bool over_limit {};
		std::string sink_error {};
		//! Streamed bodies are not retained, so keep enough of one to log.
		std::string preview {};
	};

  private:

	std::weak_ptr< Lane > m_lane;
	IoThread& m_thread;
	CurlMultiPtr m_multi {};
	std::unordered_map< CURL*, std::unique_ptr< Active > > m_active {};

	void finish( CURL* easy, CURLcode code );
	void release( Active& active );

	static std::size_t writeCallback( char* data, std::size_t size, std::size_t count, void* user );
	static std::size_t headerCallback( char* data, std::size_t size, std::size_t count, void* user );
	static int progressCallback( void* user, curl_off_t, curl_off_t, curl_off_t, curl_off_t );

  public:

	LaneShard( std::weak_ptr< Lane > lane, IoThread& thread, std::size_t host_connections );
	LaneShard( const LaneShard& ) = delete;
	LaneShard& operator=( const LaneShard& ) = delete;
	~LaneShard() override;

	[[nodiscard]] CURLM* multi() override { return m_multi.get(); }

	void onProgress() override;

	[[nodiscard]] IoThread& thread() const { return m_thread; }

	[[nodiscard]] std::size_t inFlight() const { return m_active.size(); }

	void start( TransferRequest request, TransferCallback callback );
	void cancel( const std::shared_ptr< std::atomic_bool >& cancellation );
	void shutdown();
};

} // namespace idhan::downloader
