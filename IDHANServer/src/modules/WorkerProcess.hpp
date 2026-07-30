//
// Created by kj16609 on 7/28/26.
//
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
	double timeout_multiplier { 4.0 };
	std::chrono::milliseconds max_timeout { std::chrono::minutes { 10 } };
	bool describe_only { false }; //!< Startup interrogation: announce and exit.

	//! What this library's manifest must look like, from the interrogation at startup.
	/** Checked when the worker announces itself. Module indexes only mean anything relative to the
	 *  factory that produced them, so if the .so was rebuilt while the server was running -- routine
	 *  during development -- an index registered against the old build could address a different
	 *  module in the new one. Empty disables the check, which is what interrogation itself uses. */
	std::string expected_signature {};
};

//! What a module call produced, once it has come back across the wire.
struct CallOutcome
{
	bool ok { false };
	std::string error {};
	Json::Value body {};
	ipc::Blob blob {};

	//! True when the call failed because the worker died rather than because the module said no.
	/** The pool retries these once in a fresh process; a module that simply cannot handle a file
	 *  would fail identically on a second attempt, so only this kind is worth repeating. */
	bool worker_died { false };
};

//! One IDHANModuleRunner process and the channel to it.
/** Owns a dedicated IO thread rather than sharing a drogon loop. The thread does the blocking parts
 *  -- poll, read, write -- and never runs anything that could take long, while coroutines waiting on
 *  a result are always resumed back on a drogon event loop so handler code stays where it expects to
 *  be. */
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

		//! Armed from the worker's ACK. Until then only the liveness heartbeat guards this call.
		std::chrono::steady_clock::time_point deadline { std::chrono::steady_clock::time_point::max() };
		bool acked { false };
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
	void checkDeadlines();

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
	void terminate( const std::string& reason );

	[[nodiscard]] bool alive() const { return m_alive.load(); }

	[[nodiscard]] std::size_t rssKb() const { return m_rss_kb.load(); }

	[[nodiscard]] std::size_t activeCalls() const { return m_active_calls.load(); }

	[[nodiscard]] std::chrono::steady_clock::time_point lastActivity();

	//! The manifest this worker announced, once it has arrived.
	[[nodiscard]] std::vector< ipc::ManifestEntry > manifest();

	[[nodiscard]] std::string signature();

	//! Blocks until the worker announces its manifest, or the timeout expires. Startup only.
	[[nodiscard]] std::expected< std::vector< ipc::ManifestEntry >, std::string > awaitManifest(
		std::chrono::milliseconds timeout );

	//! Queues a frame for the worker. Never blocks.
	[[nodiscard]] std::expected< void, std::string > post( const Json::Value& body, std::span< const int > fds = {} );

	//! Sends a call and suspends the calling coroutine until its result arrives.
	//! fds is taken by value rather than as a span: the coroutine suspends, and a span would point
	//! at caller storage that has long since gone out of scope by the time the frame is written.
	[[nodiscard]] IDHANTask< std::shared_ptr< CallOutcome > > call( Json::Value body, std::vector< int > fds );

	//! Allocates the next call id.
	[[nodiscard]] std::uint64_t nextCallId() { return m_next_call_id.fetch_add( 1 ); }
};

} // namespace idhan::modules
