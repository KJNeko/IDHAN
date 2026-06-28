//
// Created by kj16609 on 7/29/25.
//
#pragma once
#ifdef _WIN32

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "fgl/defines.hpp"
#include "filesystem/io/IOUring.hpp"

namespace idhan
{

//! Windows 10+ IOCP-based async I/O backend.
class IOUringW10 final : public IOUring
{
	HANDLE m_iocp { INVALID_HANDLE_VALUE };
	std::shared_ptr< std::atomic< bool > > m_running;
	std::jthread m_watcher_thread;

	friend void iocpWatcherThread(
		const std::stop_token& token,
		IOUringW10* backend,
		std::shared_ptr< std::atomic< bool > > running );

	inline static IOUringW10* s_instance { nullptr };

  public:

	FGL_DELETE_COPY( IOUringW10 );
	FGL_DELETE_MOVE( IOUringW10 );

	IOUringW10();
	~IOUringW10() override;

	//! Associates a file HANDLE with the IOCP. Must be called before any read/write on that handle.
	void associateHandle( NativeHandle handle ) override;

	drogon::Task< std::vector< std::byte > > read( NativeHandle handle, std::size_t offset, std::size_t len ) override;

	drogon::Task< void > write( NativeHandle handle, std::vector< std::byte > data, std::size_t offset ) override;

	static IOUringW10& getW10Instance();
};

} // namespace idhan

#endif // _WIN32
