#pragma once

#include <atomic>
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <semaphore>
#include <string>
#include <thread>
#include <unordered_map>
#include <variant>
#include <vector>

#include "IDHANDownloader/session/SessionObserver.hpp"
#include "js/ScriptActivity.hpp"
#include "js/ScriptContext.hpp"
#include "scripts/ScriptRegistry.hpp"

namespace idhan::downloader
{
class BytecodeCache;
class SessionContext;
class ScriptSession;
struct ScriptExecution;

struct ScriptWork
{
	std::shared_ptr< SessionContext > handle {};
	WorkID id {};
	std::optional< WorkID > parent {};
	std::string url {};
	Json::Value tags { Json::objectValue };
	//! Keep the routing decision made by submit()/follow(), including the checked parser snapshot.
	ScriptRoute route {};
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
		std::size_t parser_instances { 1 };
		ScriptContext::Options script {};
	};

  private:

	enum class WakeReason
	{
		SESSION_CAPACITY_CHANGED,
		READY_MICROTASKS,
		STOP,
	};

	using RuntimeEvent = std::variant< ScriptWork, ScriptCompletion, WakeReason >;

	//! Queues, load and notifications belong to m_mutex. The dedicated thread owns
	//! scripts/resident for their entire lifetime, including teardown.
	struct Runtime
	{
		struct Resident
		{
			std::shared_ptr< SessionContext > handle {};
			std::unique_ptr< ScriptExecution > execution {};
		};

		std::size_t id {};
		std::string parser {};
		std::counting_semaphore<> available { 0 };
		std::deque< RuntimeEvent > events {};
		std::unordered_map< const ScriptSession*, std::weak_ptr< SessionContext > > sessions {};
		std::deque< ScriptWork > work {};
		std::size_t load {};
		std::shared_ptr< ScriptActivity > activity { std::make_shared< ScriptActivity >() };
		std::unique_ptr< ScriptContext > scripts {};
		std::vector< Resident > resident {};
		std::jthread thread {};
	};

	std::shared_ptr< ParserKeyValueStore > m_key_values { std::make_shared< ParserKeyValueStore >() };
	Options m_options;
	BytecodeCache& m_bytecode;
	std::mutex m_mutex {};
	std::unordered_map< std::size_t, std::unique_ptr< Runtime > > m_runtimes {};
	std::size_t m_next_runtime {};
	std::atomic_bool m_running { true };

	void run( Runtime& runtime );

	//! Caller holds m_mutex; one permit is released for every queued event.
	void enqueueEvent( Runtime& runtime, RuntimeEvent event );
	[[nodiscard]] bool processEvent( Runtime& runtime, RuntimeEvent event );
	[[nodiscard]] bool advance( Runtime& runtime );
	void releaseAt( Runtime& runtime, std::size_t index );
	void discardAll( Runtime& runtime );

  public:

	ScriptRunner( Options options, BytecodeCache& bytecode );
	ScriptRunner( const ScriptRunner& ) = delete;
	ScriptRunner& operator=( const ScriptRunner& ) = delete;
	~ScriptRunner();

	[[nodiscard]] ParserCacheSnapshot cacheSnapshot() const { return m_key_values->snapshot(); }

	[[nodiscard]] bool running() const { return m_running.load(); }

	[[nodiscard]] bool submit( ScriptWork work );
	[[nodiscard]] bool complete( std::size_t runtime, ScriptCompletion completion );
	void discard( const SessionContext* session );
	void stop();
	void notifySessionReady( const ScriptSession* session );
};

} // namespace idhan::downloader
