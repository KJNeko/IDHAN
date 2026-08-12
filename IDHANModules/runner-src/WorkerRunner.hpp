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
#include <span>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "ModuleFile.hpp"
#include "ModuleLibrary.hpp"
#include "ipc/Blob.hpp"
#include "ipc/Frame.hpp"
#include "ipc/Protocol.hpp"
#include "ipc/UniqueFd.hpp"

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
/** The threading solves one problem: an archive thumbnailer asks the host to generate a member,
 *  which resolves back to the archive *generator* -- same library, therefore same process. A
 *  single-threaded runner would be sitting inside createThumbnailRaw, unable to serve the nested
 *  request it is itself waiting on, and would deadlock.
 *
 *  So: an IO thread that owns the socket and never runs module code, plus a pool that does. This is
 *  also where ModuleBase::threadSafe() earns its keep -- modules reporting false are serialised
 *  behind a per-module lock, everything else runs concurrently. */
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
		//! EMBED_TEXT only. The phrase to embed; that op carries no file at all.
		std::string phrase {};
		//! The input, already adopted on the IO thread: a ring must have its descriptor registered and
		//! closed before any module code runs (see RingFile::adopt). Null for EMBED_TEXT.
		std::unique_ptr< ModuleFile > file {};
	};

	//! How a callback should describe the handle a module handed it.
	/** Which case applies decides whether anything is copied at all:
	 *
	 *    - the handle *is* this call's own input: the host still holds the descriptor and only needs
	 *      telling which call to take it from (INPUT_REF). Nothing crosses the socket.
	 *    - already a memfd we hold (typically a nested generate's result): forward the descriptor.
	 *    - anything else (ModuleFile::fromBytes) must be copied into a memfd; the bytes live in this
	 *      process's heap and there is no other way out. */
	struct CallbackInput
	{
		Json::Value fields {}; //!< INPUT_REF, or INPUT_KIND plus the size.
		ipc::UniqueFd owned {}; //!< A memfd we had to create. Empty when a descriptor was reused.
		int fd { -1 }; //!< The descriptor to attach, or -1 when INPUT_REF makes one unnecessary.

		//! The descriptor list for the outgoing frame. Borrows from this object, which must therefore
		//! outlive the send -- it also owns the memfd the descriptor may refer to.
		[[nodiscard]] std::span< const int > fds() const { return { &fd, ( fd < 0 ) ? 0u : 1u }; }
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

	//! Guards the outbound queue. Frames come from the IO loop and from every pool thread, and one
	//! must not be interleaved with another.
	std::mutex m_write_mutex {};

	//! Outbound frames, drained by the IO loop only. Queued rather than written directly because the
	//! socket is non-blocking: a direct send would EAGAIN once the buffer filled, and a dropped RESULT
	//! parks the host's coroutine forever. Both ends queue, so neither can wedge the other.
	ipc::FrameWriter m_writer {};

	//! eventfd, so a pool thread queueing a result can interrupt the IO loop's poll instead of
	//! waiting for the next heartbeat tick.
	ipc::UniqueFd m_wakeup {};

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

	//! False until every module's startup() has returned. Pool threads park on this rather than
	//! serving calls; the IO loop deliberately does not wait for it -- see run().
	std::atomic< bool > m_ready { false };

	//! Declared last so it is destroyed first: the module instances hold the callbacks that capture
	//! this runner, and they must be gone before anything they might touch is.
	ModuleLibrary m_library {};

	//! Queues one frame. Never blocks and never fails on a full socket buffer.
	[[nodiscard]] std::expected< void, std::string > send( const Json::Value& body, std::span< const int > fds = {} );

	//! Writes everything queued, waiting for writability as needed. Only for paths with no IO loop to
	//! drain them: --describe, and the tail of run().
	[[nodiscard]] std::expected< void, std::string > flush();

	[[nodiscard]] ModuleCallbacks makeCallbacks();

	void handleFrame( ipc::Frame&& frame );
	void handleCall( ipc::Frame&& frame );
	void handleCallbackResult( ipc::Frame&& frame );

	//! Turns a CALL's input fields and attached descriptor into the handle the module will read.
	/** Runs on the IO thread, which is the security-relevant part for a ring: the descriptor must be
	 *  registered and closed before the call reaches a pool thread, since that is when module code
	 *  starts running, and until then the ring's fdinfo prints the registered file's path. */
	[[nodiscard]] std::expected< std::unique_ptr< ModuleFile >, std::string > adoptInput( ipc::Frame& frame );

	void workerLoop( const std::stop_token& stop );
	void runCall( QueuedCall call );

	//! Runs the requested operation, with the module's own lock held if it needs one.
	/** The result travels as a bare descriptor rather than a Blob: the worker never reads its own
	 *  output back, and mapping a generated file just to forward its descriptor would put the whole
	 *  thing back into this address space -- exactly what the sink exists to avoid. */
	[[nodiscard]] std::expected< std::pair< Json::Value, ipc::UniqueFd >, std::string > invoke(
		const QueuedCall& call,
		const std::shared_ptr< IDHANModule >& module );

	//! Works out how to ship \p file to the host, copying it only if there is no alternative.
	[[nodiscard]] std::expected< CallbackInput, std::string > describeInput( const ModuleFile& file );

	void sendHeartbeat();

	//! Wakes every parked callback with a failure. Used when the host goes away, so pool threads
	//! blocked on an answer that will never arrive can unwind instead of hanging forever.
	void failAllCallbacks( const std::string& reason );

	//! Issues a CALLBACK and blocks the calling pool thread until the host answers.
	/** \p fds are borrowed; the caller keeps ownership until this returns. */
	[[nodiscard]] std::expected< std::shared_ptr< PendingCallback >, std::string > dispatchCallback(
		ipc::CallbackKind kind,
		const Json::Value& body,
		std::span< const int > fds );

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
