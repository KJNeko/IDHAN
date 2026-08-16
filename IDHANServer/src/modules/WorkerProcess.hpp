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

struct WorkerSettings
{
	std::filesystem::path runner {};
	std::filesystem::path library {};
	std::size_t pool_threads { 4 };
	std::chrono::milliseconds heartbeat_interval { 1000 };
	std::chrono::milliseconds liveness_grace { 5000 };
	bool describe_only { false }; //!< Startup interrogation: announce and exit.

	std::string log_level { "info" };

	std::string expected_signature {};
};

enum class Termination : std::uint8_t
{
	EXPECTED,
	FAILURE
};

struct CallOutcome
{
	bool ok { false };
	std::string error {};
	Json::Value body {};
	ipc::Blob blob {};

	//! True when the call failed because the worker died rather than because the module said no.
	bool worker_died { false };
};

struct InFlightInput
{
	std::shared_ptr< const CallInput > input {};
	std::string mime {};
};

class WorkerProcess : public std::enable_shared_from_this< WorkerProcess >
{
  public:

	//! Answers a callback a module raised. Invoked on the worker's IO thread, so it must not block;
	//! implementations spawn a coroutine and return.
	using CallbackHandler = std::function< void( std::shared_ptr< WorkerProcess >, ipc::Frame ) >;

  private:

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

	[[nodiscard]] std::expected< void, std::string > start();

	//! Defaults to FAILURE so a new terminate() call site is loud unless it opts into EXPECTED.
	void terminate( const std::string& reason, Termination kind = Termination::FAILURE );

	[[nodiscard]] bool alive() const { return m_alive.load(); }

	[[nodiscard]] std::size_t rssKb() const { return m_rss_kb.load(); }

	[[nodiscard]] std::size_t activeCalls() const { return m_active_calls.load(); }

	[[nodiscard]] std::chrono::steady_clock::time_point lastActivity();

	[[nodiscard]] std::vector< ipc::ManifestEntry > manifest();

	[[nodiscard]] std::string signature();

	[[nodiscard]] bool manifestSeen();

	//! Blocks until the worker announces its manifest, or the timeout expires. Startup only.
	[[nodiscard]] std::expected< std::vector< ipc::ManifestEntry >, std::string > awaitManifest(
		std::chrono::milliseconds timeout );

	[[nodiscard]] std::expected< void, std::string > post( const Json::Value& body, std::span< const int > fds = {} );

	[[nodiscard]] IDHANTask< std::shared_ptr< CallOutcome > > call(
		Json::Value body,
		std::shared_ptr< const CallInput > input );

	//! The input of an in-flight call, for resolving INPUT_REF on a callback.
	//! A replayed frame can ask after the call has finished, in which case input is null.
	[[nodiscard]] InFlightInput inputForCall( std::uint64_t call_id );

	[[nodiscard]] std::uint64_t nextCallId() { return m_next_call_id.fetch_add( 1 ); }
};

} // namespace idhan::modules
