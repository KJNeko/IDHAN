#pragma once
#ifdef _WIN32

#include <atomic>
#include <memory>
#include <thread>

#define WIN32_LEAN_AND_MEAN
#include <ioringapi.h>
#include <windows.h>

#include "fgl/defines.hpp"
#include "filesystem/io/IOUring.hpp"

namespace idhan
{

//! Windows 11 (22H2+) IoRing-based async I/O backend.
//! Falls back to IOUringW10 automatically via IOUring::init() if unavailable.
class IOUringW11 final : public IOUring
{
	HIORING m_ring { nullptr };
	HANDLE m_completion_event { INVALID_HANDLE_VALUE };
	std::shared_ptr< std::atomic< bool > > m_running;
	std::jthread m_watcher_thread;

	friend void ioRingWatcherThread(
		const std::stop_token& token,
		IOUringW11* backend,
		std::shared_ptr< std::atomic< bool > > running );

	inline static IOUringW11* s_instance { nullptr };

  public:

	FGL_DELETE_COPY( IOUringW11 );
	FGL_DELETE_MOVE( IOUringW11 );

	//! Returns true if the IoRing API (IORING_VERSION_3) is available on this system.
	static bool isAvailable();

	IOUringW11();
	~IOUringW11() override;

	drogon::Task< std::vector< std::byte > > read( NativeHandle handle, std::size_t offset, std::size_t len ) override;

	drogon::Task< void > write( NativeHandle handle, std::vector< std::byte > data, std::size_t offset ) override;

	static IOUringW11& getW11Instance();
};

} // namespace idhan

#endif // _WIN32
