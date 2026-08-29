#pragma once

#include <curl/curl.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace idhan::downloader
{

class IoSource
{
  public:

	virtual ~IoSource() = default;

	[[nodiscard]] virtual CURLM* multi() = 0;
	virtual void onProgress() = 0;
};

class IoThread
{
	enum class EventKind : std::uint8_t
	{
		WAKEUP,
		DEADLINE,
		TIMER,
		SOCKET,
	};

	struct EventTag
	{
		EventKind kind {};
		bool detached {};
	};

	struct Registration : EventTag
	{
		IoThread* thread {};
		IoSource* source {};
		int timer_fd { -1 };
	};

	struct SocketEvent : EventTag
	{
		Registration* registration {};
		curl_socket_t socket {};
	};

  public:

	using Task = std::move_only_function< void() >;

  private:

	int m_epoll_fd { -1 };
	int m_wakeup_fd { -1 };
	int m_deadline_fd { -1 };
	std::unique_ptr< EventTag > m_wakeup_tag {};
	std::unique_ptr< EventTag > m_deadline_tag {};
	std::multimap< std::chrono::steady_clock::time_point, Task > m_timers {};
	std::unordered_map< IoSource*, std::unique_ptr< Registration > > m_sources {};
	std::vector< std::unique_ptr< SocketEvent > > m_sockets {};
	std::vector< std::unique_ptr< SocketEvent > > m_retired_sockets {};
	std::vector< std::unique_ptr< Registration > > m_retired_sources {};
	std::atomic_size_t m_load {};
	std::atomic_bool m_running { true };
	std::mutex m_tasks_mutex {};
	std::deque< Task > m_tasks {};
	std::atomic< std::thread::id > m_thread_id {};
	std::jthread m_thread {};

	void run();
	void drainTasks();
	void runDueTimers();
	void armDeadline();
	void act( IoSource& source, curl_socket_t socket, int events );
	void retireSocketsOf( const Registration& registration );

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

	[[nodiscard]] bool onThread() const { return std::this_thread::get_id() == m_thread_id.load(); }

	[[nodiscard]] std::size_t load() const { return m_load.load( std::memory_order_relaxed ); }

	void stop();
};

class IoPool
{
	std::vector< std::unique_ptr< IoThread > > m_threads {};

  public:

	explicit IoPool( std::size_t count );
	IoPool( const IoPool& ) = delete;
	IoPool& operator=( const IoPool& ) = delete;
	~IoPool();

	[[nodiscard]] IoThread& leastLoaded();

	[[nodiscard]] std::size_t size() const { return m_threads.size(); }

	void stop();

	static void initialiseCurl();
	[[nodiscard]] static bool supportsHttp3();
	[[nodiscard]] static bool supportsHttp2();
};

} // namespace idhan::downloader
