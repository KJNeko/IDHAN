#pragma once

#include <folly/Function.h>
#include <folly/io/async/AsyncTimeout.h>
#include <folly/io/async/EventBase.h>
#include <folly/io/async/EventHandler.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <thread>
#include <unordered_map>
#include <vector>

#include "IoSource.hpp"

namespace idhan::downloader
{

class IoThread
{
	struct SourceState;

	struct SocketHandler : folly::EventHandler
	{
		SocketHandler( folly::EventBase* base, curl_socket_t socket, SourceState& owner );

		void handlerReady( std::uint16_t events ) noexcept override;

		curl_socket_t socket;
		SourceState& owner;
	};

	struct SourceState : folly::AsyncTimeout
	{
		SourceState( folly::EventBase* base, IoThread& thread, IoSource& source );

		void timeoutExpired() noexcept override;

		IoThread& thread;
		IoSource& source;
		std::unordered_map< curl_socket_t, std::unique_ptr< SocketHandler > > sockets {};
	};

  public:

	using Task = folly::Function< void() >;

  private:

	folly::EventBase m_event_base {};
	std::unordered_map< IoSource*, std::unique_ptr< SourceState > > m_sources {};
	std::vector< std::unique_ptr< SocketHandler > > m_retired_sockets {};
	bool m_cleanup_scheduled { false };
	std::atomic_size_t m_load {};
	std::atomic< std::thread::id > m_thread_id {};
	std::jthread m_thread {};

	void run();
	void act( SourceState& state, curl_socket_t socket, int events );
	void scheduleCleanup();

	static int socketCallback( CURL* easy, curl_socket_t socket, int what, void* user, void* socket_user );
	static int timerCallback( CURLM* multi, long timeout_ms, void* user );

  public:

	IoThread();
	IoThread( const IoThread& ) = delete;
	IoThread& operator=( const IoThread& ) = delete;
	~IoThread();

	void post( Task task );
	void postAfter( std::chrono::steady_clock::duration delay, Task task );

	void attach( IoSource& source );
	void detach( IoSource& source );

	[[nodiscard]] bool onThread() const { return m_event_base.isInEventBaseThread(); }

	[[nodiscard]] std::size_t load() const { return m_load.load( std::memory_order_relaxed ); }

	void stop();
};

} // namespace idhan::downloader
