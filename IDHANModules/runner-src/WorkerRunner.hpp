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

struct RunnerOptions
{
	std::filesystem::path library {};
	int socket_fd { -1 };
	bool describe_only { false };
	std::size_t pool_threads { 4 };
	//! Threads a module may use inside one call. Zero leaves each library at its own default.
	std::size_t render_threads { 0 };
	std::chrono::milliseconds heartbeat_interval { 1000 };
};

class WorkerRunner
{
	struct QueuedCall
	{
		std::uint64_t call_id { 0 };
		std::size_t module_index { 0 };
		ipc::CallOp op { ipc::CallOp::METADATA };
		MimeID mime_id { 0 };
		Json::Value extra {};
		std::size_t width { 0 };
		std::size_t height { 0 };
		std::array< std::byte, 256 / 8 > hash {};
		std::uint32_t depth { 0 };
		std::string phrase {};
		std::unique_ptr< ModuleFile > file {};
	};

	struct CallbackInput
	{
		Json::Value fields {}; //!< INPUT_REF, or INPUT_KIND plus the size.
		ipc::UniqueFd owned {}; //!< A memfd we had to create. Empty when a descriptor was reused.
		int fd { -1 }; //!< The descriptor to attach, or -1 when INPUT_REF makes one unnecessary.

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

	std::mutex m_write_mutex {};

	ipc::FrameWriter m_writer {};

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

	std::atomic< bool > m_ready { false };

	ModuleLibrary m_library {};

	[[nodiscard]] std::expected< void, std::string > send( const Json::Value& body, std::span< const int > fds = {} );

	[[nodiscard]] std::expected< void, std::string > flush();

	[[nodiscard]] ModuleCallbacks makeCallbacks();

	void handleFrame( ipc::Frame&& frame );
	void handleCall( ipc::Frame&& frame );
	void handleCallbackResult( ipc::Frame&& frame );

	[[nodiscard]] std::expected< std::unique_ptr< ModuleFile >, std::string > adoptInput( ipc::Frame& frame );

	void workerLoop( const std::stop_token& stop );
	void runCall( QueuedCall call );

	[[nodiscard]] std::expected< std::pair< Json::Value, ipc::UniqueFd >, std::string > invoke(
		const QueuedCall& call,
		const std::shared_ptr< IDHANModule >& module );

	[[nodiscard]] std::expected< CallbackInput, std::string > describeInput( const ModuleFile& file );

	void sendHeartbeat();

	void failAllCallbacks( const std::string& reason );

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

	[[nodiscard]] std::expected< void, std::string > load();

	[[nodiscard]] Json::Value manifestJson() const;

	[[nodiscard]] std::expected< void, std::string > describe();

	[[nodiscard]] std::expected< void, std::string > run();
};

[[nodiscard]] std::size_t residentSetKb();

} // namespace idhan::runner
