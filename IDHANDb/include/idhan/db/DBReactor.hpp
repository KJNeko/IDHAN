#pragma once
#include <folly/executors/CPUThreadPoolExecutor.h>

#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace idhan::db
{
class DBConnection;
enum class SocketInterest
{
	read,
	write,
	read_write
};

class DBReactor
{
	struct Watch
	{
		std::shared_ptr< DBConnection > connection {};
		int socket { -1 };
		SocketInterest interest { SocketInterest::read };
		std::atomic< bool > scheduled { false };
		std::atomic< bool > pending { false };
	};

	int m_epoll_fd { -1 };
	int m_wake_fd { -1 };
	std::unique_ptr< folly::CPUThreadPoolExecutor > m_workers {};
	std::jthread m_poller {};
	std::mutex m_mutex {};
	std::unordered_map< DBConnection*, std::shared_ptr< Watch > > m_watches {};
	std::atomic< bool > m_started { false };
	std::atomic< bool > m_stopping { false };

	void notify() noexcept;
	void request( const std::shared_ptr< Watch >& );
	void run();

	// Locked m_watches lookups/mutations shared by the public watch API below.
	[[nodiscard]] std::shared_ptr< Watch > findWatch( DBConnection* );
	[[nodiscard]] std::shared_ptr< Watch > findOrCreateWatch( std::shared_ptr< DBConnection > );
	[[nodiscard]] std::shared_ptr< Watch > removeWatch( DBConnection& );
	// registerWatch/updateWatch hold the lock across their epoll_ctl call, so a
	// concurrent rearm/unwatch for the same connection can't race the decision
	// between EPOLL_CTL_ADD and EPOLL_CTL_MOD.
	void registerWatch( std::shared_ptr< DBConnection >, int socket, SocketInterest );
	void updateWatch( DBConnection&, SocketInterest );

  public:

	explicit DBReactor( std::size_t worker_threads );
	~DBReactor();
	void start();
	void watch( std::shared_ptr< DBConnection >, int socket, SocketInterest );
	void rearm( DBConnection&, SocketInterest );
	void unwatch( DBConnection& );
	void schedule( std::shared_ptr< DBConnection > );
	void stop();
};
} // namespace idhan::db
