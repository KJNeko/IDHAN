//
// Created by kj16609 on 7/28/26.
//
#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
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

#include "ModuleLibrary.hpp"
#include "ipc/Blob.hpp"
#include "ipc/Frame.hpp"
#include "ipc/Protocol.hpp"

namespace idhan::runner
{

//! Settings the server passes on the command line.
struct RunnerOptions
{
	std::filesystem::path library {};
	int socket_fd { -1 };
	bool describe_only { false };
	std::size_t pool_threads { 4 };
	std::chrono::milliseconds heartbeat_interval { 1000 };
};

//! Hosts one module library and serves calls for it over the worker channel.
/** The structure exists to solve one specific problem. An archive thumbnailer asks the host to
 *  generate a member, and the host resolves that back to the archive *generator* -- a different
 *  module in the same library, therefore this same process. If the runner were single-threaded it
 *  would be sitting inside createThumbnailRaw, unable to serve the nested request it is itself
 *  waiting on, and would deadlock.
 *
 *  So: an IO thread that owns the socket and never runs module code, and a pool of worker threads
 *  that do. The nested call arrives while a pool thread is blocked, and runs on a different one.
 *
 *  This is also where ModuleBase::threadSafe() finally earns its keep -- modules that report false
 *  are serialised behind a per-module lock, everything else runs concurrently. */
class WorkerRunner
{
	//! A call waiting for, or occupying, a pool thread.
	struct QueuedCall
	{
		std::uint64_t call_id { 0 };
		std::size_t module_index { 0 };
		ipc::CallOp op { ipc::CallOp::METADATA };
		std::string mime {};
		Json::Value extra {};
		std::size_t width { 0 };
		std::size_t height { 0 };
		std::array< std::byte, 256 / 8 > hash {};
		std::uint32_t depth { 0 };
		ipc::Blob blob {};
	};

	//! A callback a module issued, parked until the host answers it.
	struct PendingCallback
	{
		std::mutex mutex {};
		std::condition_variable ready {};
		bool answered { false };
		bool ok { false };
		std::string error {};
		Json::Value body {};
		ipc::Blob blob {};
	};

	RunnerOptions m_options;

	int m_socket { -1 };
	ipc::FrameReader m_reader {};

	//! Serialises writes. Frames come from the IO thread (ACK-adjacent traffic, heartbeats) and from
	//! every pool thread (results, callbacks), and a frame must not be interleaved with another.
	//!
	//! Writes are blocking, which is safe only because the *server* never blocks writing to us: it
	//! queues outbound frames on its event loop. If both ends blocked on full socket buffers at once
	//! they would deadlock, so that asymmetry is load-bearing, not incidental.
	std::mutex m_write_mutex {};

	std::mutex m_queue_mutex {};
	std::condition_variable m_queue_ready {};
	std::deque< QueuedCall > m_queue {};
	std::vector< std::jthread > m_pool {};

	//! One lock per module, used only for modules that report threadSafe() == false.
	std::vector< std::unique_ptr< std::recursive_mutex > > m_module_locks {};

	std::mutex m_callback_mutex {};
	std::unordered_map< std::uint64_t, std::shared_ptr< PendingCallback > > m_callbacks {};
	std::uint64_t m_next_callback_id { 1 };

	std::atomic< bool > m_stopping { false };
	std::atomic< std::size_t > m_active_calls { 0 };

	//! Declared last so it is destroyed first: the module instances hold the callbacks that capture
	//! this runner, and they must be gone before anything they might touch is.
	ModuleLibrary m_library {};

	[[nodiscard]] std::expected< void, std::string > send( const Json::Value& body, std::span< const int > fds = {} );

	[[nodiscard]] ModuleCallbacks makeCallbacks();

	void handleFrame( ipc::Frame&& frame );
	void handleCall( ipc::Frame&& frame );
	void handleCallbackResult( ipc::Frame&& frame );

	void workerLoop( const std::stop_token& stop );
	void runCall( QueuedCall call );

	//! Runs the requested operation, with the module's own lock held if it needs one.
	[[nodiscard]] std::expected< std::pair< Json::Value, ipc::Blob >, std::string > invoke(
		const QueuedCall& call,
		const std::shared_ptr< IDHANModule >& module );

	void sendHeartbeat();

	//! Wakes every parked callback with a failure. Used when the host goes away, so pool threads
	//! blocked on an answer that will never arrive can unwind instead of hanging forever.
	void failAllCallbacks( const std::string& reason );

	//! Issues a CALLBACK and blocks the calling pool thread until the host answers.
	[[nodiscard]] std::expected< std::shared_ptr< PendingCallback >, std::string > dispatchCallback(
		ipc::CallbackKind kind,
		const Json::Value& body,
		const ipc::Blob& payload );

  public:

	explicit WorkerRunner( RunnerOptions options );
	~WorkerRunner() = default;

	WorkerRunner( const WorkerRunner& ) = delete;
	WorkerRunner& operator=( const WorkerRunner& ) = delete;
	WorkerRunner( WorkerRunner&& ) = delete;
	WorkerRunner& operator=( WorkerRunner&& ) = delete;

	//! Loads the library and announces it. Must succeed before run() or describe() is called.
	[[nodiscard]] std::expected< void, std::string > load();

	//! The MANIFEST body describing every module this library exports.
	[[nodiscard]] Json::Value manifestJson() const;

	//! Sends the manifest and returns, for --describe.
	[[nodiscard]] std::expected< void, std::string > describe();

	//! Serves calls until the host asks us to stop or closes the channel.
	[[nodiscard]] std::expected< void, std::string > run();
};

//! Resident set size of this process in kibibytes, as reported to the host in each heartbeat.
[[nodiscard]] std::size_t residentSetKb();

} // namespace idhan::runner
