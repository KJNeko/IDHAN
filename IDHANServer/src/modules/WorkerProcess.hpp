#pragma once

#include <atomic>
#include <chrono>
#include <coroutine>
#include <cstdint>
#include <deque>
#include <expected>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "CallInput.hpp"
#include "ipc/Blob.hpp"
#include "ipc/Frame.hpp"
#include "ipc/Protocol.hpp"
#include "threading/IDHANTask.hpp"

namespace trantor
{
class EventLoop;
}

namespace idhan::modules
{

//! Everything a worker needs to be spawned, from config plus the library path.
struct WorkerSettings
{
	std::filesystem::path runner {}; //!< Path to IDHANModuleRunner.
	std::filesystem::path library {}; //!< The .so this worker hosts.
	std::size_t pool_threads { 4 };
	std::chrono::milliseconds heartbeat_interval { 1000 };
	std::chrono::milliseconds liveness_grace { 5000 };
	bool describe_only { false }; //!< Startup interrogation: announce and exit.

	//! spdlog level name the worker starts at.
	std::string log_level { "info" };

	//! What this library's manifest must look like, from the interrogation at startup.
	std::string expected_signature {};
};

//! Why a worker is being torn down.
enum class Termination : std::uint8_t
{
	EXPECTED,
	FAILURE
};

//! What a module call produced, once it has come back across the wire.
struct CallOutcome
{
	bool ok { false };
	std::string error {};
	Json::Value body {};
	ipc::Blob blob {};

	//! True when the call failed because the worker died rather than because the module said no.
	bool worker_died { false };
};

//! A call's input, held for as long as the call is in flight.
struct InFlightInput
{
	std::shared_ptr< const CallInput > input {};
	std::string mime {};
};

//! One IDHANModuleRunner process and the channel to it.
class WorkerProcess : public std::enable_shared_from_this< WorkerProcess >
{
  public:

	//! Answers a callback a module raised. Invoked on the worker's IO thread, so it must not block;
	//! implementations spawn a coroutine and return.
	using CallbackHandler = std::function< void( std::shared_ptr< WorkerProcess >, ipc::Frame ) >;

  private:

	//! A call that has been sent and is waiting for its RESULT.
	struct PendingCall
	{
		std::coroutine_handle<> continuation {};
		trantor::EventLoop* loop { nullptr };
		std::shared_ptr< CallOutcome > outcome {};
	};

	WorkerSettings m_settings;
	CallbackHandler m_on_callback;

	pid_t m_pid { -1 };
	ipc::UniqueFd m_socket {};
	ipc::UniqueFd m_wakeup {}; //!< eventfd, so enqueuing a frame can interrupt the IO thread's poll.

	ipc::FrameReader m_reader {};

	std::mutex m_write_mutex {};
	ipc::FrameWriter m_writer {};

	std::mutex m_calls_mutex {};
	std::unordered_map< std::uint64_t, PendingCall > m_calls {};
	//! The input of every in-flight call, so a module passing its own input back through a callback is
	//! answered by reusing it.
	std::unordered_map< std::uint64_t, InFlightInput > m_call_inputs {};
	std::atomic< std::uint64_t > m_next_call_id { 1 };

	std::atomic< bool > m_alive { false };

	std::jthread m_io {};

	std::mutex m_manifest_mutex {};
	std::vector< ipc::ManifestEntry > m_manifest {};
	std::string m_signature {};
	bool m_manifest_seen { false };

	std::atomic< std::size_t > m_rss_kb { 0 };
	std::atomic< std::size_t > m_active_calls { 0 };
	std::chrono::steady_clock::time_point m_last_heartbeat {};
	std::chrono::steady_clock::time_point m_last_activity {};

	void ioLoop( const std::stop_token& stop );
	void handleFrame( ipc::Frame&& frame );
	void finish( std::uint64_t call_id, CallOutcome outcome );
	void failAll( const std::string& reason, bool died );
	void checkLiveness();

  public:

	WorkerProcess( WorkerSettings settings, CallbackHandler on_callback );
	~WorkerProcess();

	WorkerProcess( const WorkerProcess& ) = delete;
	WorkerProcess& operator=( const WorkerProcess& ) = delete;
	WorkerProcess( WorkerProcess&& ) = delete;
	WorkerProcess& operator=( WorkerProcess&& ) = delete;

	//! Forks and execs the runner, then starts the IO thread.
	[[nodiscard]] std::expected< void, std::string > start();

	//! Kills the process and fails everything outstanding.
	/** \param kind Whether this is routine teardown or a failure. Only affects how it is logged, and
	 *              defaults to FAILURE so a new call site is loud rather than silent. */
	void terminate( const std::string& reason, Termination kind = Termination::FAILURE );

	[[nodiscard]] bool alive() const { return m_alive.load(); }

	[[nodiscard]] std::size_t rssKb() const { return m_rss_kb.load(); }

	[[nodiscard]] std::size_t activeCalls() const { return m_active_calls.load(); }

	[[nodiscard]] std::chrono::steady_clock::time_point lastActivity();

	//! The manifest this worker announced, once it has arrived.
	[[nodiscard]] std::vector< ipc::ManifestEntry > manifest();

	[[nodiscard]] std::string signature();

	//! Whether this worker has announced its manifest yet.
	[[nodiscard]] bool manifestSeen();

	//! Blocks until the worker announces its manifest, or the timeout expires. Startup only.
	[[nodiscard]] std::expected< std::vector< ipc::ManifestEntry >, std::string > awaitManifest(
		std::chrono::milliseconds timeout );

	//! Queues a frame for the worker. Never blocks.
	[[nodiscard]] std::expected< void, std::string > post( const Json::Value& body, std::span< const int > fds = {} );

	//! Sends a call and suspends the calling coroutine until its result arrives.
	[[nodiscard]] IDHANTask< std::shared_ptr< CallOutcome > > call(
		Json::Value body,
		std::shared_ptr< const CallInput > input );

	//! The input of an in-flight call, for resolving INPUT_REF on a callback.
	/** \return An entry with a null input if the call already finished. A module cannot cause that,
	 *          since it is blocked inside that very call, but a replayed frame could ask for it. */
	[[nodiscard]] InFlightInput inputForCall( std::uint64_t call_id );

	//! Allocates the next call id.
	[[nodiscard]] std::uint64_t nextCallId() { return m_next_call_id.fetch_add( 1 ); }
};

} // namespace idhan::modules
