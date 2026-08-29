#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "IDHANDownloader/SessionObserver.hpp"
#include "js/ScriptContext.hpp"

namespace idhan::downloader
{
class BytecodeCache;
class SessionContext;
struct ScriptExecution;

struct ScriptWork
{
	std::shared_ptr< SessionContext > handle {};
	WorkID id {};
	std::optional< WorkID > parent {};
	std::string url {};
};

struct ScriptCompletion
{
	std::shared_ptr< SessionContext > handle {};
	std::uint64_t pending {};
};

class ScriptRunner
{
  public:

	struct Options
	{
		std::size_t threads { 4 };
		ScriptContext::Options script {};
	};

  private:

	class Worker
	{
		ScriptRunner& m_runner;
		std::size_t m_index;
		std::unique_ptr< ScriptContext > m_scripts {};

		std::vector< std::unique_ptr< ScriptExecution > > m_resident {};
		std::vector< std::shared_ptr< SessionContext > > m_handles {};

		mutable std::mutex m_mutex {};
		std::condition_variable m_wakeup {};
		std::deque< ScriptCompletion > m_inbox {};
		bool m_woken {};
		std::jthread m_thread {};

		void run();
		void drainInbox();
		void startWork();
		void advance();
		void releaseAt( std::size_t index );
		void discardAll();

	  public:

		Worker( ScriptRunner& runner, std::size_t index );
		Worker( const Worker& ) = delete;
		Worker& operator=( const Worker& ) = delete;
		~Worker();

		void deliver( ScriptCompletion completion );
		void notify();
		void join();
	};

	Options m_options;
	BytecodeCache& m_bytecode;

	mutable std::mutex m_mutex {};
	std::condition_variable m_drained {};
	std::deque< ScriptWork > m_queue {};
	std::atomic_bool m_running { true };
	std::vector< std::unique_ptr< Worker > > m_workers {};

	[[nodiscard]] std::optional< ScriptWork > take();

  public:

	ScriptRunner( Options options, BytecodeCache& bytecode );
	ScriptRunner( const ScriptRunner& ) = delete;
	ScriptRunner& operator=( const ScriptRunner& ) = delete;
	~ScriptRunner();

	[[nodiscard]] BytecodeCache& bytecode() const { return m_bytecode; }

	[[nodiscard]] std::size_t workers() const { return m_workers.size(); }

	[[nodiscard]] bool submit( ScriptWork work );
	[[nodiscard]] bool complete( std::size_t worker, ScriptCompletion completion );
	void discard( const SessionContext* session );

	void stop();
};

} // namespace idhan::downloader
