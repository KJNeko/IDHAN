#pragma once

#include <curl/curl.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>

#include "http/CurlHandles.hpp"
#include "http/IoSource.hpp"
#include "http/IoThread.hpp"
#include "http/Transfer.hpp"

namespace idhan::downloader
{
class Lane;

class LaneShard final : public IoSource, public std::enable_shared_from_this< LaneShard >
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
		bool paused {};
		std::shared_ptr< std::atomic_bool > alive { std::make_shared< std::atomic_bool >( true ) };
		bool stopped_on_not_found {};
		std::string sink_error {};
		//! Streamed bodies are not retained, so keep enough of one to log.
		std::string preview {};

		Active() = default;
		Active( const Active& ) = delete;
		Active& operator=( const Active& ) = delete;
	};

  private:

	std::weak_ptr< Lane > m_lane;
	IoThread& m_thread;
	CurlMultiPtr m_multi {};
	//! Includes transfers posted to the IO thread but not started yet.
	std::atomic_size_t m_assigned {};
	std::unordered_map< CURL*, std::unique_ptr< Active > > m_active {};

	void finish( CURL* easy, CURLcode code );
	void release( Active& active );
	void resume( CURL* easy, std::shared_ptr< std::atomic_bool > alive );

	static void recordProgress( Active& active, std::size_t bytes );
	static std::size_t writeCallback( char* data, std::size_t size, std::size_t count, void* user );
	static std::size_t headerCallback( char* data, std::size_t size, std::size_t count, void* user );
	static int debugCallback( CURL* easy, curl_infotype type, char* data, std::size_t size, void* user );
	static int progressCallback( void* user, curl_off_t, curl_off_t, curl_off_t, curl_off_t );

  public:

	LaneShard( std::weak_ptr< Lane > lane, IoThread& thread, std::size_t host_connections );
	LaneShard( const LaneShard& ) = delete;
	LaneShard& operator=( const LaneShard& ) = delete;
	~LaneShard() override;

	[[nodiscard]] CURLM* multi() override { return m_multi.get(); }

	void onProgress() override;

	[[nodiscard]] IoThread& thread() const { return m_thread; }

	[[nodiscard]] std::size_t inFlight() const { return m_assigned.load( std::memory_order_relaxed ); }

	void reserve() { m_assigned.fetch_add( 1, std::memory_order_relaxed ); }

	void start( TransferRequest request, TransferCallback callback );
	void cancel( const std::shared_ptr< std::atomic_bool >& cancellation );
	void shutdown();
};

} // namespace idhan::downloader
