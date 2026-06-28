//
// Created by kj16609 on 7/29/25.
//
#pragma once
#ifdef _WIN32

#include <coroutine>
#include <exception>
#include <memory>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "fgl/defines.hpp"

namespace trantor
{
class EventLoop;
}

namespace idhan
{

//! Common header embedded as the FIRST member of both Windows awaiters.
//! IOCP watcher casts lpOverlapped back to this to dispatch to the right awaiter type.
struct WinAwaiterBase
{
	OVERLAPPED m_overlapped {};  // MUST be first member

	enum class Type : std::uint8_t
	{
		READ,
		WRITE
	} m_type;

	trantor::EventLoop* m_event_loop { nullptr };
	std::coroutine_handle<> m_cont {};
	std::exception_ptr m_exception {};

	explicit WinAwaiterBase( Type t ) : m_type( t ) {}
};

// ─── ReadAwaiterWin ───────────────────────────────────────────────────────────

struct [[nodiscard]] ReadAwaiterWin : WinAwaiterBase
{
	std::shared_ptr< std::vector< std::byte > > m_data {};
	bool m_completed_sync { false };

	FGL_DELETE_COPY( ReadAwaiterWin );
	FGL_DELETE_MOVE( ReadAwaiterWin );

	explicit ReadAwaiterWin( std::shared_ptr< std::vector< std::byte > > data );

	void complete( DWORD bytes_transferred, bool success );

	[[nodiscard]] bool await_ready() const noexcept;
	void await_suspend( std::coroutine_handle<> h );
	std::vector< std::byte > await_resume() const;

	~ReadAwaiterWin() = default;
};

// ─── WriteAwaiterWin ──────────────────────────────────────────────────────────

struct [[nodiscard]] WriteAwaiterWin : WinAwaiterBase
{
	bool m_completed_sync { false };

	FGL_DELETE_COPY( WriteAwaiterWin );
	FGL_DELETE_MOVE( WriteAwaiterWin );

	WriteAwaiterWin();

	void complete( DWORD bytes_transferred, bool success );

	[[nodiscard]] static bool await_ready() noexcept;
	void await_suspend( std::coroutine_handle<> h );
	void await_resume() const;

	~WriteAwaiterWin() = default;
};

} // namespace idhan

#endif // _WIN32
